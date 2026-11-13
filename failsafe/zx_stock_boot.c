// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Wifi Chan Lab
 * Platform: Sanechip ZX297520V3 (Cortex-A53, 32-bit AArch32)
 * Stock Factory Firmware Loader
 *
 * Extracts and reassembles factory firmware components from the JFFS2 'imagefs'
 * partition:
 *   - evb_cpurpm.img                  (Cortex-M0 PMU firmware)
 *   - cpuphy_00.lzma ~ cpuphy_08.lzma (ZSP modem DSP firmware slices)
 *   - cpuap_00.lzma  ~ cpuap_21.lzma  (Linux AP kernel uImage slices)
 */

#include <command.h>
#include <env.h>
#include <asm/io.h>
#include <asm/global_data.h>
#include <version.h>
#include <image.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <watchdog.h>
#include <cpu_func.h>
#include <lzma/LzmaTools.h>
#include <lzma/LzmaDec.h>
#include <u-boot/crc.h>
#include <u-boot/zlib.h>
#include <net.h>
#include "zx_flash.h"
#include "zx_stock_boot.h"

DECLARE_GLOBAL_DATA_PTR;

/* DRAM memory layout for stock bootloader */
#define RAW_IMAGEFS_RAM_ADDR      0x20000000   /* 5.5MB temporary buffer for raw flash partition */
#define KERNEL_ASSEMBLE_BASE      0x20C00000   /* Assembled kernel image (CONFIG_SYS_SDRAM_IMAGEFS_BASE) */
#define KERNEL_UIMAGE_ADDR        (KERNEL_ASSEMBLE_BASE + 384) /* uImage header starts after 384-byte ZTE signature */
#define M0_FIRMWARE_RAM_ADDR      0x21400000   /* M0 PMU firmware image (CONFIG_SYS_SDRAM_TEMP_BASE) */
#define SLICE_TEMP_RAM_ADDR       0x21480000   /* 256KB temporary slice buffer */
#define ZSP_DECOMP_RAM_ADDR       0x21600000   /* ZSP decompressed buffer (CONFIG_SYS_SDRAM_TEMP_LZMA) */

#define M0_REMAP_REG              0x00140000
#define M0_FLAG_ADDR              0x82002cf4

/* JFFS2 structures */
struct jffs2_unknown_node {
	__u16 magic;
	__u16 nodetype;
	__u32 totlen;
	__u32 hdr_crc;
} __attribute__((packed));

struct jffs2_raw_dirent {
	__u16 magic;
	__u16 nodetype;
	__u32 totlen;
	__u32 hdr_crc;
	__u32 pino;
	__u32 version;
	__u32 ino;
	__u32 mctime;
	__u8  nsize;
	__u8  type;
	__u8  unused[2];
	__u32 node_crc;
	__u32 name_crc;
	__u8  name[0];
} __attribute__((packed));

struct jffs2_raw_inode {
	__u16 magic;
	__u16 nodetype;
	__u32 totlen;
	__u32 hdr_crc;
	__u32 ino;
	__u32 version;
	__u32 mode;
	__u16 uid;
	__u16 gid;
	__u32 isize;
	__u32 atime;
	__u32 mtime;
	__u32 ctime;
	__u32 offset;
	__u32 csize;
	__u32 dsize;
	__u8  compr;
	__u8  usercompr;
	__u16 flags;
	__u32 data_crc;
	__u32 node_crc;
	__u8  data[0];
} __attribute__((packed));

static void jffs2_rtime_decompress(const u8 *data_in, u8 *cpage_out,
				   u32 srclen, u32 destlen)
{
	u16 positions[256];
	int outpos = 0;
	int pos = 0;

	memset(positions, 0, sizeof(positions));

	while (outpos < (int)destlen && pos < (int)srclen) {
		u8 value = data_in[pos++];
		int backoffs = positions[value];
		int repeat;

		cpage_out[outpos++] = value;
		if (pos >= (int)srclen)
			break;
		repeat = data_in[pos++];
		positions[value] = outpos;
		if (repeat) {
			if (backoffs + repeat >= outpos) {
				while (repeat && outpos < (int)destlen) {
					cpage_out[outpos++] = cpage_out[backoffs++];
					repeat--;
				}
			} else {
				if (outpos + repeat > (int)destlen)
					repeat = destlen - outpos;
				memcpy(&cpage_out[outpos], &cpage_out[backoffs], repeat);
				outpos += repeat;
			}
		}
	}
}

static int jffs2_zlib_decompress(const u8 *data_in, u8 *cpage_out,
				 u32 srclen, u32 destlen)
{
	z_stream strm;
	int ret;

	if (srclen <= 2)
		return -1;

	memset(&strm, 0, sizeof(strm));
	/* Skip 2-byte zlib header and use raw deflate (-MAX_WBITS) */
	if (inflateInit2(&strm, -MAX_WBITS) != Z_OK)
		return -1;

	strm.next_in = (Bytef *)(data_in + 2);
	strm.avail_in = srclen - 2;
	strm.next_out = cpage_out;
	strm.avail_out = destlen;

	ret = inflate(&strm, Z_FINISH);
	inflateEnd(&strm);

	return (ret == Z_STREAM_END || ret == Z_OK) ? 0 : -1;
}

static u32 zx_jffs2_find_ino(const u8 *data, size_t len, const char *name)
{
	size_t pos = 0;
	size_t nlen = strlen(name);
	u32 best_ver = 0;
	u32 best_ino = 0;

	while (pos + sizeof(struct jffs2_unknown_node) <= len) {
		const struct jffs2_unknown_node *un =
			(const struct jffs2_unknown_node *)(data + pos);

		if (un->magic != 0x1985 || un->totlen < sizeof(struct jffs2_unknown_node) ||
		    pos + un->totlen > len) {
			pos += 4;
			continue;
		}

		if (crc32_no_comp(0, (const u8 *)un, sizeof(struct jffs2_unknown_node) - 4) != un->hdr_crc) {
			pos += 4;
			continue;
		}

		if (un->nodetype == 0xe001 && pos + sizeof(struct jffs2_raw_dirent) <= len) {
			const struct jffs2_raw_dirent *rd = (const struct jffs2_raw_dirent *)un;

			if (pos + sizeof(struct jffs2_raw_dirent) + rd->nsize <= len &&
			    rd->nsize == nlen &&
			    memcmp(rd->name, name, nlen) == 0 &&
			    crc32_no_comp(0, (const u8 *)rd, sizeof(struct jffs2_raw_dirent) - 8) == rd->node_crc &&
			    crc32_no_comp(0, rd->name, rd->nsize) == rd->name_crc) {
				if (rd->version >= best_ver) {
					best_ver = rd->version;
					best_ino = rd->ino;
				}
			}
		}

		pos += (un->totlen + 3) & ~3;
	}
	return best_ino;
}

#define MAX_INODE_NODES 512

static int zx_jffs2_extract(const u8 *data, size_t len, u32 target_ino,
			    u8 *out_buf, size_t max_out, size_t *out_len)
{
	static const struct jffs2_raw_inode *nodes[MAX_INODE_NODES];
	static u8 lzma_temp_in[4096 + 13];
	size_t pos = 0;
	size_t total = 0;
	int count = 0;
	int i, j;

	while (pos + sizeof(struct jffs2_unknown_node) <= len) {
		const struct jffs2_unknown_node *un =
			(const struct jffs2_unknown_node *)(data + pos);

		if (un->magic != 0x1985 || un->totlen < sizeof(struct jffs2_unknown_node) ||
		    pos + un->totlen > len) {
			pos += 4;
			continue;
		}

		if (crc32_no_comp(0, (const u8 *)un, sizeof(struct jffs2_unknown_node) - 4) != un->hdr_crc) {
			pos += 4;
			continue;
		}

		if (un->nodetype == 0xe002 && pos + sizeof(struct jffs2_raw_inode) <= len) {
			const struct jffs2_raw_inode *in = (const struct jffs2_raw_inode *)un;

			if (in->ino == target_ino && in->dsize > 0 &&
			    pos + sizeof(struct jffs2_raw_inode) + in->csize <= len &&
			    crc32_no_comp(0, (const u8 *)in, sizeof(struct jffs2_raw_inode) - 8) == in->node_crc) {
				const u8 *payload = data + pos + sizeof(struct jffs2_raw_inode);

				if (in->csize == 0 ||
				    crc32_no_comp(0, payload, in->csize) == in->data_crc) {
					if (count < MAX_INODE_NODES)
						nodes[count++] = in;
				}
			}
		}

		pos += (un->totlen + 3) & ~3;
	}

	/* Sort matching inode nodes by version ascending so newer log nodes win */
	for (i = 1; i < count; i++) {
		const struct jffs2_raw_inode *key = nodes[i];

		j = i - 1;
		while (j >= 0 && nodes[j]->version > key->version) {
			nodes[j + 1] = nodes[j];
			j--;
		}
		nodes[j + 1] = key;
	}

	for (i = 0; i < count; i++) {
		const struct jffs2_raw_inode *in = nodes[i];
		const u8 *payload = (const u8 *)in + sizeof(struct jffs2_raw_inode);

		if (in->offset + in->dsize > max_out)
			continue;

		if (in->compr == 0) {
			memcpy(out_buf + in->offset, payload, in->dsize);
		} else if (in->compr == 1) {
			/* JFFS2_COMPR_ZERO */
			memset(out_buf + in->offset, 0, in->dsize);
		} else if (in->compr == 2) {
			/* JFFS2_COMPR_RTIME */
			jffs2_rtime_decompress(payload, out_buf + in->offset,
					       in->csize, in->dsize);
		} else if (in->compr == 6) {
			/* JFFS2_COMPR_ZLIB */
			jffs2_zlib_decompress(payload, out_buf + in->offset,
					      in->csize, in->dsize);
		} else if (in->compr == 8 && in->csize <= 4096) {
			/*
			 * JFFS2 LZMA decompression: reconstruct 13-byte LZMA_Alone header
			 * JFFS2 LZMA properties: lc=0, lp=0, pb=0 (props=0x00), dictSize=8192 (0x2000)
			 */
			SizeT uncomp = in->dsize;

			memset(lzma_temp_in, 0, 13);
			lzma_temp_in[0] = 0x00; /* props: lc=0, lp=0, pb=0 */
			lzma_temp_in[1] = 0x00;
			lzma_temp_in[2] = 0x20; /* dict size 0x00002000 (8192 bytes = 8KB) */
			lzma_temp_in[3] = 0x00;
			lzma_temp_in[4] = 0x00;
			lzma_temp_in[5] = in->dsize & 0xff;
			lzma_temp_in[6] = (in->dsize >> 8) & 0xff;
			lzma_temp_in[7] = (in->dsize >> 16) & 0xff;
			lzma_temp_in[8] = (in->dsize >> 24) & 0xff;
			memcpy(lzma_temp_in + 13, payload, in->csize);
			lzmaBuffToBuffDecompress(out_buf + in->offset, &uncomp,
						 lzma_temp_in, in->csize + 13);
		}

		if (in->offset + in->dsize > total)
			total = in->offset + in->dsize;
	}

	if (out_len)
		*out_len = total;
	return total > 0 ? 0 : -1;
}

static int zx_init_m0(const u8 *m0_data, size_t m0_len)
{
	int timeout = 500000;

	printf("[STOCK_BOOT] Starting M0 PMU subsystem (%zu bytes)...\n", m0_len);

	const struct legacy_img_hdr *m0_hdr = (const struct legacy_img_hdr *)m0_data;
	if (image_get_magic(m0_hdr) == IH_MAGIC) {
		printf("[STOCK_BOOT] M0 Image Validated: \"%s\", ep: 0x%08x\n",
		       image_get_name(m0_hdr), image_get_ep(m0_hdr));
	} else {
		printf("[STOCK_BOOT] Warning: M0 Image magic 0x%08x != 0x%08x\n",
		       image_get_magic(m0_hdr), IH_MAGIC);
	}

	if (m0_data != (const u8 *)M0_FIRMWARE_RAM_ADDR)
		memcpy((void *)M0_FIRMWARE_RAM_ADDR, m0_data, m0_len);
	flush_dcache_all();

	/* Set M0 remap register (bit 23 = 0x800000) as in factory u-boot fs_load_m0_image */
	u32 remap = readl((void __iomem *)M0_REMAP_REG);
	remap |= 0x800000;
	writel(remap, (void __iomem *)M0_REMAP_REG);

	/* Probe M0 Heartbeat before signaling */
	u32 flag_before = readl(M0_FLAG_ADDR);
	u32 hb1 = readl((void __iomem *)(M0_FLAG_ADDR + 4));
	udelay(5000);
	u32 hb2 = readl((void __iomem *)(M0_FLAG_ADDR + 4));
	u32 last_seen = readl((void __iomem *)(M0_FLAG_ADDR + 8));
	printf("[STOCK_BOOT] M0 flag: 0x%08x, Heartbeat: hb1=0x%08x, hb2=0x%08x (delta=%u), last_read=0x%08x\n",
	       flag_before, hb1, hb2, hb2 - hb1, last_seen);

	/* Send signal (1) to M0 as in factory u-boot load.c */
	writel(1, (void __iomem *)M0_FLAG_ADDR);
	flush_dcache_all();

	/* 原厂 load.c:471(写 1 后忙等该标志被 M0 清零)，不做额外条件判断 */
	while (--timeout) {
		if (readl(M0_FLAG_ADDR) == 0)
			break;
		udelay(10);
	}

	if (timeout == 0) {
		u32 hb_after = readl((void __iomem *)(M0_FLAG_ADDR + 4));
		printf("[STOCK_BOOT] Warning: M0 acknowledgment timed out! (flag=0x%08x, ack=0x%08x, hb_after=0x%08x, last_read=0x%08x)\n",
		       readl(M0_FLAG_ADDR), readl((void __iomem *)(M0_FLAG_ADDR + 12)), hb_after, readl((void __iomem *)(M0_FLAG_ADDR + 8)));
		return -1;
	}
	printf("[STOCK_BOOT] M0 subsystem running!\n");
	return 0;
}

static int reform_zsp_image(u32 addr)
{
	u32 offset = addr;
	u16 w0 = *(volatile u16 *)offset;
	u16 w1 = *(volatile u16 *)(offset + 2);
	u32 arm_size = w0 | ((u32)w1 << 16);
	int length = 0;

	offset += 4;
	while (length < (int)(arm_size - 4)) {
		u16 d0 = *(volatile u16 *)(offset + 4);
		u16 d1 = *(volatile u16 *)(offset + 6);
		u32 dest = (d0 | ((u32)d1 << 16)) * 2;

		u16 l0 = *(volatile u16 *)(offset + 8);
		u16 l1 = *(volatile u16 *)(offset + 10);
		u32 len = (l0 | ((u32)l1 << 16)) * 2;

		if (dest >= 0x20000000)
			memcpy((void *)dest, (const void *)(offset + 12), len);

		length += len + 12;
		offset += len + 12;
	}
	return 0;
}

int zx_boot_stock_linux(void)
{
	u32 imgfs_offset = 0, imgfs_size = 0;
	u8 *raw_data = (u8 *)RAW_IMAGEFS_RAM_ADDR;
	u8 *slice_buf = (u8 *)SLICE_TEMP_RAM_ADDR;
	u8 *kernel_dest = (u8 *)KERNEL_ASSEMBLE_BASE;
	size_t extracted_sz = 0;
	char fname[32];
	u32 ino;
	int ret;

	printf("\n[STOCK_BOOT] =========================================\n");
	printf("[STOCK_BOOT]   ZTE Stock Factory Firmware Loader      \n");
	printf("[STOCK_BOOT] =========================================\n");
	printf("[STOCK_BOOT] [HB-START] M0 HB at loader start: 0x%08x\n", readl((void __iomem *)(M0_FLAG_ADDR + 4)));

	if (zx_flash_find_partition("imagefs", &imgfs_offset, &imgfs_size) != 0 || imgfs_size == 0) {
		printf("[STOCK_BOOT] Partition 'imagefs' not found in partition table!\n");
		return -1;
	}

	/*
	 * Cap raw_data buffer at 8MB (0x20000000..0x20800000) so large NAND
	 * imagefs partitions (28MB/64MB) never overwrite KERNEL_ASSEMBLE_BASE
	 * (0x20C00000) or U-Boot text (0x21DF0000). Also stop early after 4
	 * consecutive erased 128KB blocks.
	 */
	if (imgfs_size > 0x00800000)
		imgfs_size = 0x00800000;

	printf("[STOCK_BOOT] Reading 'imagefs' from flash 0x%08x (up to %u KB) ...\n",
	       imgfs_offset, imgfs_size / 1024);
	{
		u32 read_off = 0;
		u32 step = 128 * 1024;
		int empty_blocks = 0;

		ret = 0;
		while (read_off < imgfs_size) {
			u32 cur = (imgfs_size - read_off > step) ? step : (imgfs_size - read_off);
			u32 phys = zx_flash_logical_to_physical(imgfs_offset, read_off);
			u32 *w = (u32 *)(raw_data + read_off);
			u32 k, all_ff = 1;

			if (zx_flash_read(phys, raw_data + read_off, cur) != 0) {
				ret = -1;
				break;
			}
			for (k = 0; k < cur / 4; k++) {
				if (w[k] != 0xFFFFFFFF) {
					all_ff = 0;
					break;
				}
			}
			read_off += cur;
			if (all_ff) {
				if (++empty_blocks >= 4 && read_off >= 0x100000)
					break;
			} else {
				empty_blocks = 0;
			}
		}
		imgfs_size = read_off;
	}
	printf("[STOCK_BOOT] [HB-FLASH] M0 HB after flash_read (%u KB): 0x%08x\n",
	       imgfs_size / 1024, readl((void __iomem *)(M0_FLAG_ADDR + 4)));
	if (ret != 0) {
		printf("[STOCK_BOOT] Error: Failed to read imagefs partition!\n");
		return -1;
	}

	/* 1. M0 PMU Firmware */
	ino = zx_jffs2_find_ino(raw_data, imgfs_size, "evb_cpurpm.img");
	if (ino) {
		printf("[STOCK_BOOT] Extracting M0 PMU firmware ...\n");
		if (zx_jffs2_extract(raw_data, imgfs_size, ino, (u8 *)M0_FIRMWARE_RAM_ADDR, 0x40000, &extracted_sz) == 0)
			zx_init_m0((const u8 *)M0_FIRMWARE_RAM_ADDR, extracted_sz);
	} else {
		printf("[STOCK_BOOT] Info: evb_cpurpm.img not present, skipping M0\n");
	}

	/* 2. Linux Kernel AP Firmware: Extract & Decompress slices to 0x20C00000 */
	printf("[STOCK_BOOT] Decompressing Linux kernel slices ...\n");
	for (int i = 0; i < 24; i++) {
		snprintf(fname, sizeof(fname), "cpuap_%02d.lzma", i);
		ino = zx_jffs2_find_ino(raw_data, imgfs_size, fname);
		if (!ino) {
			if (i == 0) {
				printf("[STOCK_BOOT] Error: cpuap_00.lzma not found!\n");
				return -1;
			}
			break;
		}

		if (zx_jffs2_extract(raw_data, imgfs_size, ino, slice_buf, 0x40000, &extracted_sz) != 0) {
			printf("[STOCK_BOOT] Error extracting %s!\n", fname);
			return -1;
		}

		SizeT uncomp = 256 * 1024;
		ret = lzmaBuffToBuffDecompress(kernel_dest, &uncomp, slice_buf, extracted_sz);
		if (ret != 0) {
			printf("[STOCK_BOOT] LZMA decompression error %d on %s!\n", ret, fname);
			return -1;
		}
		kernel_dest += uncomp;
		schedule();
	}

	/* 3. ZSP Modem DSP Firmware: Extract & Decompress to 0x21600000 */
	ino = zx_jffs2_find_ino(raw_data, imgfs_size, "cpuphy_00.lzma");
	if (ino) {
		u8 *zsp_dest = (u8 *)ZSP_DECOMP_RAM_ADDR;
		printf("[STOCK_BOOT] Loading ZSP modem DSP firmware ...\n");
		for (int i = 0; i < 9; i++) {
			snprintf(fname, sizeof(fname), "cpuphy_%02d.lzma", i);
			ino = zx_jffs2_find_ino(raw_data, imgfs_size, fname);
			if (!ino)
				break;
			if (zx_jffs2_extract(raw_data, imgfs_size, ino, slice_buf, 0x40000, &extracted_sz) == 0) {
				SizeT uncomp = 256 * 1024;
				ret = lzmaBuffToBuffDecompress(zsp_dest, &uncomp, slice_buf, extracted_sz);
				if (ret == 0)
					zsp_dest += uncomp;
			}
		}
		/* 4. All reads from raw_data (0x20000000) are complete! Now map ZSP into target memory.
		 * 入口 = 解压缓冲区起始（偏移 0）。实测验证：cpuphy_00..08 解压拼接后
		 * 在偏移 0 处 arm_size == 缓冲区全长，且段表全部指向 DDR(>=0x20000000)；
		 * 若加任何头部偏移（384/448/640）解析出的 arm_size 与段地址均为垃圾。
		 */
		reform_zsp_image(ZSP_DECOMP_RAM_ADDR);
		printf("[STOCK_BOOT] ZSP modem DSP initialized!\n");
	}

	/* 4. Validate U-Boot uImage Header */
	const struct legacy_img_hdr *hdr = (const struct legacy_img_hdr *)KERNEL_UIMAGE_ADDR;
	if (image_get_magic(hdr) != IH_MAGIC) {
		printf("[STOCK_BOOT] Error: Invalid uImage magic 0x%08x at 0x%08x!\n",
		       image_get_magic(hdr), KERNEL_UIMAGE_ADDR);
		return -1;
	}

	printf("[STOCK_BOOT] Kernel Validated: \"%s\"\n", image_get_name(hdr));
	printf("[STOCK_BOOT] Kernel Size: %u bytes, Load: 0x%08x, Entry: 0x%08x\n",
	       image_get_size(hdr), image_get_load(hdr), image_get_ep(hdr));

	/* 内核镜像保持原厂内容，不做任何二进制补丁。
	 * 原厂内核的 SPI 驱动依赖真正等待 SPI 控制器完成；此前把 spifc_wait_cmd_end
	 * 改成“立即返回成功”会让内核读 flash 拿到非法数据，导致 rootfs 挂载失败。
	 */
	u8 *kdata = (u8 *)(KERNEL_UIMAGE_ADDR + sizeof(struct legacy_img_hdr));
	struct legacy_img_hdr *k_hdr = (struct legacy_img_hdr *)KERNEL_UIMAGE_ADDR;

	/* 重新计算 uImage CRC（镜像未被修改时与原厂值等价，仅作防御性保证） */
	image_set_dcrc(k_hdr, crc32(0, kdata, image_get_size(k_hdr)));
	image_set_hcrc(k_hdr, 0);
	image_set_hcrc(k_hdr, crc32(0, (const u8 *)k_hdr, sizeof(struct legacy_img_hdr)));
	flush_dcache_all();

	/* 5. Set Kernel Arguments and Boot */
	char mtdparts_buf[512];
	char full_bootargs[768];
	if (zx_flash_generate_mtdparts(mtdparts_buf, sizeof(mtdparts_buf)) == 0) {
		snprintf(full_bootargs, sizeof(full_bootargs),
			 ZX_STD_BOOTARGS_BASE " %s", mtdparts_buf);
	} else {
		snprintf(full_bootargs, sizeof(full_bootargs),
			 "%s", ZX_STD_BOOTARGS_BASE);
	}
	env_set("bootargs", full_bootargs);
	env_set_hex("machid", 7523);
	if (gd && gd->bd) {
		/* 与原厂一致（load.c: sys_ddr_kernel_start = ih_ep - 0x8000）：
		 *   ATAG_MEM.start = sys_ddr_kernel_start
		 *   ATAG_MEM.size  = DDR 顶端 - sys_ddr_kernel_start
		 *   bi_boot_params = sys_ddr_kernel_start + 0x100
		 */
		ulong kstart  = image_get_ep(hdr) - 0x8000;
		ulong ddr_top = 0x20000000UL + (gd->ram_size ? gd->ram_size : 0x02000000UL);

		gd->bd->bi_arch_number = 7523;
#if (U_BOOT_VERSION_NUM > 2026) || (U_BOOT_VERSION_NUM == 2026 && U_BOOT_VERSION_NUM_PATCH >= 7)
		gd->dram[0].start = kstart;
		gd->dram[0].size  = ddr_top - kstart;
#else
		gd->bd->bi_dram[0].start = kstart;
		gd->bd->bi_dram[0].size  = ddr_top - kstart;
#endif
		gd->bd->bi_boot_params   = kstart + 0x100;
	}

#if (U_BOOT_VERSION_NUM > 2026) || (U_BOOT_VERSION_NUM == 2026 && U_BOOT_VERSION_NUM_PATCH >= 7)
	printf("[STOCK_BOOT] Linux ATAG_MEM: start=0x%08lx, size=%lu KB (AP RAM)\n",
	       (unsigned long)gd->dram[0].start,
	       (unsigned long)(gd->dram[0].size / 1024));
#else
	printf("[STOCK_BOOT] Linux ATAG_MEM: start=0x%08lx, size=%lu KB (AP RAM)\n",
	       (unsigned long)gd->bd->bi_dram[0].start,
	       (unsigned long)(gd->bd->bi_dram[0].size / 1024));
#endif
	printf("[STOCK_BOOT] Bootargs: %s\n", full_bootargs);

	/* Hardware clock and reset initialization from stock firmware for Linux AP */
	writel(0x01000075, (void __iomem *)0x01400058);
	writel(0x00010319, (void __iomem *)0x0140005c);
	writel((readl((void __iomem *)0x01303000) & 0xFFFE7FFF) | 0x8000, (void __iomem *)0x01303000);
	writel(readl((void __iomem *)0x0013C030) | 1, (void __iomem *)0x0013C030);

	/* Factory PS_HOLD (GPIO 24) pull on (pmu_pull_on_ps_hold from u-boot-master/drivers/power/zx234290.c):
	 * 1. Disable pull-up/pull-down: IO_CFG_BASE+0x4 (0x0013C804) bits 29:28 = 0
	 * 2. Set direction: GPIOPDD_REG0(24) = 0x0013D040 bit 8 = 0 (input / reversed for PS_HOLD)
	 * 3. Set output high: SET1_SEND_REG0(24) = 0x0013D058 bit 8 = 1
	 */
	writel(readl((void __iomem *)0x0013C804) & ~(0x3 << 28), (void __iomem *)0x0013C804);
	writel(readl((void __iomem *)0x0013D040) & ~(1 << 8), (void __iomem *)0x0013D040);
	writel(1 << 8, (void __iomem *)0x0013D058);

	/* Write POWER_ON_NORMAL (0) to POWERON_TYPE_BASE in IRAM0 (0x82002BC0) */
	writel(0, (void __iomem *)0x82002BC0);

	/* Enable and ungate TOP_CRM Peripherals (0x0013B000) */
	/* 0x0013B060: AP Timers 1, 2, 3 (en=bits 0,1, 4,5, 8,9; auto-gate disable=bits 2, 6, 10) */
	writel((readl((void __iomem *)0x0013B060) & ~0x00000444) | 0x00000333, (void __iomem *)0x0013B060);
	/* 0x0013B044: AP Timers 1, 2, 3 Clock Source (select 26MHz: bits 0, 1, 2) */
	writel(readl((void __iomem *)0x0013B044) | 0x00000007, (void __iomem *)0x0013B044);
	/* 0x0013B050: AP Timers 1, 2, 3 Clock Dividers (clear to 0 = divide by 1) */
	writel(readl((void __iomem *)0x0013B050) & ~0x00000FFF, (void __iomem *)0x0013B050);
	/* 0x0013B054: I2C0 (PMIC I2C) & RTC (en=bits 0,1, 8,9; auto-gate disable=bits 10, 11) */
	writel((readl((void __iomem *)0x0013B054) & ~0x00000C00) | 0x00000303, (void __iomem *)0x0013B054);
	/* 0x0013B05C: UART0 & KPD (en=bits 12,13, 16,17; auto-gate disable=bits 14, 15) */
	writel((readl((void __iomem *)0x0013B05C) & ~0x0000C000) | 0x00033000, (void __iomem *)0x0013B05C);

	/* Factory I2C pinmux configuration (u-boot-master/drivers/i2c/i2c.c): */
	/* GPIO43 (SCL0) & GPIO44 (SDA0): 0x0013C004 bits 3:0 = 0 */
	writel(readl((void __iomem *)0x0013C004) & ~0x0000000F, (void __iomem *)0x0013C004);
	/* GPIO45 (SCL1) & GPIO46 (SDA1): 0x0013C020 bits 15:14 = 1 */
	writel(readl((void __iomem *)0x0013C020) | (0x3 << 14), (void __iomem *)0x0013C020);

	/* Factory PMIC I2C0 clock configuration (0x0013B03C, 0x0013B074, 0x0013B054): */
	writel(readl((void __iomem *)0x0013B03C) & ~(1 << 1), (void __iomem *)0x0013B03C); /* 26M select */
	writel(readl((void __iomem *)0x0013B074) | (1 << 8) | (1 << 9), (void __iomem *)0x0013B074); /* reset deassert */
	writel(readl((void __iomem *)0x0013B054) | (1 << 8) | (1 << 9), (void __iomem *)0x0013B054); /* clk enable */

	/* Enable and ungate LSP Peripherals in LSP_CRPM (0x01400000) */
	/* 0x01400024: SPIFC (SPI Flash Controller: en=bits 0,1, div=2 (bits 6:4 = 2) for 124.8MHz; auto-gate disable=bits 10,11) */
	writel((readl((void __iomem *)0x01400024) & ~0x00000C70) | 0x00000023, (void __iomem *)0x01400024);
	/* Configure GPIO93..98 pinmux to SPIFC function (HW CS) */
	writel(readl((void __iomem *)0x0013C028) | (0x3F << 6), (void __iomem *)0x0013C028);
	/* Pre-initialize SPIFC timing and control */
	writel((1 << 16) | (2 << 11) | (2 << 6) | (4 << 0), (void __iomem *)0x01407024); /* SFC_TIMING */
	writel(readl((void __iomem *)0x01407008) | 1, (void __iomem *)0x01407008); /* SFC_EN */
	writel(readl((void __iomem *)0x0140700C) | (1 << 16), (void __iomem *)0x0140700C); /* FC_SCLK_PAUSE_EN */
	/* 0x01400028: UART1 (en=bits 0,1, sel=bit 4; auto-gate disable=bits 10, 11) */
	writel((readl((void __iomem *)0x01400028) & ~0x00000C00) | 0x00000013, (void __iomem *)0x01400028);
	/* 0x0140002C: I2C1 (factory setup: 26M select, reset deassert, clk enable) */
	writel((readl((void __iomem *)0x0140002C) & ~((1 << 4) | 0x00000C00)) | (1 << 8) | (1 << 9) | 0x3, (void __iomem *)0x0140002C);
	/* 0x01400030: SSP0 */
	writel((readl((void __iomem *)0x01400030) & ~0x00000C00) | 0x00000013, (void __iomem *)0x01400030);
	/* 0x0140003C: UART2 */
	writel((readl((void __iomem *)0x0140003C) & ~0x00000C00) | 0x00000013, (void __iomem *)0x0140003C);
	/* 0x01400040: AP Watchdog */
	writel((readl((void __iomem *)0x01400040) & ~0x00000C00) | 0x00000013, (void __iomem *)0x01400040);
	/* 0x01400044: Timer0 (en=bits 0,1, sel=bit 4; auto-gate disable=bit 11) */
	writel((readl((void __iomem *)0x01400044) & ~0x00000800) | 0x00000013, (void __iomem *)0x01400044);
	/* 0x01400048: SSP1 */
	writel((readl((void __iomem *)0x01400048) & ~0x00000C00) | 0x00000013, (void __iomem *)0x01400048);
	/* 0x0140004C: Timer4 (en=bits 0,1, sel=bit 4; auto-gate disable=bit 11) */
	writel((readl((void __iomem *)0x0140004C) & ~0x00000800) | 0x00000013, (void __iomem *)0x0140004C);
	/* 0x01400050: TDM */
	writel((readl((void __iomem *)0x01400050) & ~0x00000C00) | 0x00000003, (void __iomem *)0x01400050);

	/* Clear hardware spinlock registers (SHARED_DEVICE_REG1 ~ REG8: 0x01306170..0x0130618C) */
	for (int i = 0; i < 8; i++)
		writel(0, (void __iomem *)(0x01306170 + i * 4));

	/* Clear softspinlock shared descriptors in IRAM0 (0x820029C0, 256 bytes) */
	memset((void *)0x820029C0, 0, 0x100);

	/* Quiesce active network/USB gadget DMA and interrupts before booting Linux */
	eth_halt();
	writel(0, (void __iomem *)0x01500018); /* DWC2 GINTMSK = 0 */
	writel(readl((void __iomem *)0x01500804) | 0x2, (void __iomem *)0x01500804); /* DWC2 DCTL SFTDISCON */

	printf("[STOCK_BOOT] Launching Linux kernel via bootm 0x%08x ...\n\n", KERNEL_UIMAGE_ADDR);
	snprintf(fname, sizeof(fname), "bootm 0x%08x", KERNEL_UIMAGE_ADDR);
	run_command(fname, 0);

	return 0;
}

static int do_stock_boot(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	return zx_boot_stock_linux();
}

U_BOOT_CMD(
	stock_boot, 1, 0, do_stock_boot,
	"Boot ZTE stock Linux from imagefs",
	""
);

