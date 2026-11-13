// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Wifi Chan Lab
 * Platform: Sanechip ZX297520V3 (Cortex-A53, 32-bit AArch32)
 * Unified SPI NOR / SPI NAND Flash Controller Driver
 *
 * Register definitions and command sequences follow the Sanechip ZX297520V3
 * SPIFC controller specification (LSP_CRPM 0x01400000, SPIFC 0x01407000).
 */

#include <asm/io.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <time.h>
#include <watchdog.h>
#include <malloc.h>
#include <vsprintf.h>
#include "zx_flash.h"
#include <failsafe/fw_type.h>

static void zx_flash_probe_partition_table(void);

#define ZX_SPIFC_BASE           0x01407000

/* Register offsets */
#define SFC_VER_REG             0x00
#define SFC_START               0x04
#define SFC_EN                  0x08
#define SFC_CTRL0               0x0C
#define SFC_CTRL1               0x10
#define SFC_CTRL2               0x14
#define SFC_BYTE_NUM            0x18
#define SFC_ADDR                0x1C
#define SFC_INS                 0x20
#define SFC_TIMING              0x24
#define SFC_INT_EN              0x28
#define SFC_INT_RAW             0x2C
#define SFC_INT_SW_CLR          0x30
#define SFC_SW                  0x34
#define SFC_DATA                0x38

/* Bit definitions */
#define FC_START                (1 << 0)
#define FC_BUSY                 (1 << 0)
#define FC_EN_BIT               (1 << 0)
#define FC_EN_BACK              (1 << 1)

#define FC_SCLK_PAUSE_CLR_ALLOW (1 << 17)
#define FC_TXFIFO_CLR           (1 << 15)
#define FC_RXFIFO_CLR           (1 << 14)
#define FC_TXFIFO_THRES         (10)
#define FC_RXFIFO_THRES         (6)

#define FC_ADDR_TX_EN           (1 << 4)
#define FC_DUMMY_TX_EN          (1 << 2)
#define FC_READ_DAT_EN          (1 << 1)
#define FC_WRITE_DAT_EN         (1 << 0)

#define FC_INT_RAW_CMD_END      (1 << 0)

#define FC_RX_FIFO_CNT_MASK     (0x1F)
#define FC_RX_FIFO_CNT_SHIFT    8
#define FC_TX_FIFO_CNT_MASK     (0x1F)
#define FC_TX_FIFO_CNT_SHIFT    16

/* Standard SPI NOR Opcodes */
#define NOR_CMD_WREN            0x06
#define NOR_CMD_WRDI            0x04
#define NOR_CMD_RDSR            0x05
#define NOR_CMD_RDSR2           0x35
#define NOR_CMD_WRSR            0x01
#define NOR_CMD_READ            0x03
#define NOR_CMD_FAST_READ       0x0B
#define NOR_CMD_FAST_READ_QUAD  0x6B   /* 1-line cmd/addr, 4-line data, 1 dummy */
#define NOR_CMD_PP              0x02
#define NOR_CMD_PP_QUAD         0x32   /* 1-line cmd/addr, 4-line data */
#define NOR_CMD_SE_4K           0x20
#define NOR_CMD_BE_32K          0x52
#define NOR_CMD_BE_64K          0xD8
#define NOR_CMD_CE              0xC7
#define NOR_CMD_RDID            0x9F

/* Standard SPI NAND Opcodes */
#undef NAND_CMD_RESET
#define NAND_CMD_RESET          0xFF
#define NAND_CMD_RDID           0x9F
#define NAND_CMD_GET_FEATURE    0x0F
#define NAND_CMD_SET_FEATURE    0x1F
#define NAND_CMD_WREN           0x06
#define NAND_CMD_PAGE_READ      0x13
#define NAND_CMD_READ_CACHE     0x03
#define NAND_CMD_PROG_LOAD      0x02
#define NAND_CMD_PROG_EXEC      0x10
#define NAND_CMD_BLOCK_ERASE    0xD8

#define NAND_REG_PROTECTION     0xA0
#define NAND_REG_CONFIG         0xB0
#define NAND_REG_STATUS         0xC0
#define NAND_STATUS_OIP         (1 << 0)
#define NAND_STATUS_WEL         (1 << 1)
#define NAND_STATUS_E_FAIL      (1 << 2)
#define NAND_STATUS_P_FAIL      (1 << 3)

static int g_use_quad = 0;
/* Why quad is on/off -- surfaced in the diagnostic string */
static char g_quad_reason[80] = "not probed";

static struct zx_flash_desc g_flash = {
	.type = FLASH_TYPE_UNKNOWN,
	.initialized = 0,
};

static inline void spifc_write(u32 reg, u32 val)
{
	writel(val, (void __iomem *)(ZX_SPIFC_BASE + reg));
}

static inline u32 spifc_read(u32 reg)
{
	return readl((void __iomem *)(ZX_SPIFC_BASE + reg));
}

static int spifc_wait_cmd_end(void)
{
	int timeout = 2000000;
	/* Wait for controller to finish busy state (SFC_START bit 0 clears) */
	while ((spifc_read(SFC_START) & FC_START) && --timeout)
		;

	/* Then poll on CMD_END interrupt (bit 0 in SFC_INT_RAW) */
	while (!(spifc_read(SFC_INT_RAW) & FC_INT_RAW_CMD_END) && --timeout)
		;

	u32 raw = spifc_read(SFC_INT_RAW);
	spifc_write(SFC_INT_SW_CLR, raw ? raw : 0xFF);
	if (!timeout) {
		printf("[SPIFC] timeout waiting CMD_END (raw=0x%08x, START=0x%08x, SW=0x%08x)\n",
		       raw, spifc_read(SFC_START), spifc_read(SFC_SW));
		return -ETIMEDOUT;
	}

	return 0;
}

static int g_cs_gpio86 = 0;
static char g_flash_probe_diag[256] = "Unprobed";

/*
 * Configure SPIFC functional clock divider in LSP_CRPM (0x01400024).
 * Bits [6:4] select the SPIFC divider (2 = 124.8 MHz, 3 = 104 MHz).
 * Mask only bits [6:4] (0x70) to preserve Timer0 clock settings in bits [19:16].
 */
static void spifc_set_clock(u32 clk_div)
{
	writel((readl((void __iomem *)0x01400024) & ~0x70) | ((clk_div & 0x7) << 4),
	       (void __iomem *)0x01400024);
	udelay(50);
}

static void spifc_set_cs_mode(int use_gpio86)
{
	g_cs_gpio86 = use_gpio86;
	if (use_gpio86) {
		/* Pinmux GPIO86 to GPIO function */
		writel((readl((void __iomem *)0x0013C00C) & ~(0x3 << 16)) | (0x1 << 16),
		       (void __iomem *)0x0013C00C);
		/* Pinmux GPIO93 away from SPIFC CS to GPIO function */
		writel(readl((void __iomem *)0x0013C028) & ~(1 << 6),
		       (void __iomem *)0x0013C028);
		/* Set GPIO0 direction: GPIO86 (bit 6) and GPIO93 (bit 13) as OUTPUT (1) */
		writel(readl((void __iomem *)0x0013D140) | (1 << 6) | (1 << 13),
		       (void __iomem *)0x0013D140);
		/* Deassert both: drive HIGH */
		writel(1 << 13, (void __iomem *)0x0013D158);
		writel(1 << 6, (void __iomem *)0x0013D158);
	} else {
		/* Hardware CS on GPIO93 (bits 11:6 in 0x0013C028 to 0x3F) */
		writel(readl((void __iomem *)0x0013C028) | (0x3F << 6),
		       (void __iomem *)0x0013C028);
	}
	udelay(10);
}

static inline void nor_cs_assert(void)
{
	if (g_cs_gpio86) {
		writel(1 << 6, (void __iomem *)0x0013D15C); /* GPIO86 LOW */
		udelay(1);
	}
}

static inline void nor_cs_deassert(void)
{
	if (g_cs_gpio86) {
		udelay(1);
		writel(1 << 6, (void __iomem *)0x0013D158); /* GPIO86 HIGH */
		udelay(1);
	}
}

static void spifc_hwinit(void)
{
	/* 1. Pinmux: Configure GPIO 93..98 (CS, CLK, DATA0..3) to SPIFC function */
	writel(readl((void __iomem *)0x0013C028) | (0x3F << 6), (void __iomem *)0x0013C028);

	/* 2. Clock: Default to 124.8 MHz (SPIFC_CLKSEL bits[6:4] = 2) */
	spifc_set_clock(2);

	/* 3. Enable controller and configure timing */
	if (spifc_read(SFC_EN) & 1)
		spifc_write(SFC_EN, spifc_read(SFC_EN) & ~1);

	spifc_write(SFC_EN, spifc_read(SFC_EN) | 1);
	spifc_write(SFC_CTRL0, spifc_read(SFC_CTRL0) | (1 << 16)); /* FC_SCLK_PAUSE_EN */
	spifc_write(SFC_TIMING, (1 << 16) | (2 << 11) | (2 << 6) | (4 << 0));
}

static void nor_reset_controller(void)
{
	if (spifc_read(SFC_EN) & 1)
		spifc_write(SFC_EN, spifc_read(SFC_EN) & ~1);

	spifc_write(SFC_EN, spifc_read(SFC_EN) | 1);
	spifc_write(SFC_CTRL0, spifc_read(SFC_CTRL0) | (1 << 16));
}

static void spifc_setup_ext(u8 opcode, u32 addr, int addr_len, int dummy_bytes,
			    int read_en, int write_en, u32 len, int is_quad)
{
	u32 ctrl1 = 0, ctrl2 = 0;

	/* Clear DMA bits, clear FIFO, clear interrupts */
	spifc_write(SFC_CTRL0, spifc_read(SFC_CTRL0) & ~((1 << 4) | (1 << 5)));
	spifc_write(SFC_CTRL0, spifc_read(SFC_CTRL0) | FC_RXFIFO_CLR | FC_TXFIFO_CLR | (1 << FC_RXFIFO_THRES) | (1 << FC_TXFIFO_THRES));
	spifc_write(SFC_INT_SW_CLR, 0xFF);

	if (addr_len > 0)
		ctrl1 |= FC_ADDR_TX_EN;
	if (dummy_bytes > 0)
		ctrl1 |= FC_DUMMY_TX_EN;
	if (read_en)
		ctrl1 |= FC_READ_DAT_EN;
	if (write_en)
		ctrl1 |= FC_WRITE_DAT_EN;

	spifc_write(SFC_CTRL1, ctrl1);

	if (dummy_bytes > 0)
		ctrl2 |= (dummy_bytes & 0xF) << 12;
	if (addr_len == 1)
		ctrl2 |= (0 << 5);
	else if (addr_len == 2)
		ctrl2 |= (1 << 5);
	else if (addr_len == 3)
		ctrl2 |= (2 << 5);
	else if (addr_len == 4)
		ctrl2 |= (3 << 5);

	/* TRANS_MOD: SINGLE mode is 2; QUAD mode is 5 (matching ZTE AM206 sub_21E07930) */
	if (is_quad)
		ctrl2 |= 5;
	else
		ctrl2 |= 2;

	spifc_write(SFC_CTRL2, ctrl2);
	spifc_write(SFC_BYTE_NUM, len ? len - 1 : 0);
	spifc_write(SFC_ADDR, addr);
	spifc_write(SFC_INS, opcode);
}

static inline void spifc_setup(u8 opcode, u32 addr, int addr_len, int dummy_bytes,
			       int read_en, int write_en, u32 len)
{
	spifc_setup_ext(opcode, addr, addr_len, dummy_bytes, read_en, write_en, len, 0);
}

static int spifc_read_fifo(u8 *buf, u32 len)
{
	u32 words = (len + 3) / 4;
	u32 i = 0, timeout = 500000;
	u32 tmp[64];
	u32 *p = (words <= 64) ? tmp : malloc(words * 4);

	if (!p)
		return -ENOMEM;

	while (i < words && --timeout) {
		if ((spifc_read(SFC_SW) >> FC_RX_FIFO_CNT_SHIFT) & FC_RX_FIFO_CNT_MASK) {
			p[i++] = spifc_read(SFC_DATA);
		}
	}
	if (!timeout) {
		printf("[SPIFC] read_fifo timeout (words got: %u/%u, SFC_SW=0x%08x)\n",
		       i, words, spifc_read(SFC_SW));
		if (p != tmp)
			free(p);
		return -ETIMEDOUT;
	}

	memcpy(buf, p, len);
	if (p != tmp)
		free(p);
	return 0;
}

static int spifc_write_fifo(const u8 *buf, u32 len)
{
	u32 words = (len + 3) / 4;
	u32 tmp[64];
	u32 *p = (words <= 64) ? tmp : malloc(words * 4);

	if (!p)
		return -ENOMEM;

	memset(p, 0xFF, words * 4);
	memcpy(p, buf, len);

	for (u32 i = 0; i < words; i++) {
		spifc_write(SFC_DATA, p[i]);
	}
	if (p != tmp)
		free(p);
	return 0;
}

/* ========================================================================= */
/* SPI NOR Flash Implementation                                              */
/* ========================================================================= */

static int nor_write_enable(void)
{
	nor_cs_assert();
	spifc_setup(NOR_CMD_WREN, 0, 0, 0, 0, 0, 0);
	spifc_write(SFC_START, FC_START);
	int ret = spifc_wait_cmd_end();
	nor_cs_deassert();
	return ret;
}

static int nor_wait_wip(void);

static u8 nor_read_status(void)
{
	u8 status = 0xFF;
	nor_cs_assert();
	spifc_setup(NOR_CMD_RDSR, 0, 0, 0, 1, 0, 1);
	spifc_write(SFC_START, FC_START);
	if (spifc_wait_cmd_end() == 0)
		spifc_read_fifo(&status, 1);
	nor_cs_deassert();
	return status;
}

/* Status register 2 (RDSR2 0x35) -- carries the Quad Enable (QE) bit 1 */
static u8 nor_read_status2(void)
{
	u8 status = 0xFF;
	nor_cs_assert();
	spifc_setup(NOR_CMD_RDSR2, 0, 0, 0, 1, 0, 1);
	spifc_write(SFC_START, FC_START);
	if (spifc_wait_cmd_end() == 0)
		spifc_read_fifo(&status, 1);
	nor_cs_deassert();
	return status;
}

/* Write both status bytes (WRSR 0x01 + SR1 + SR2) */
static int nor_write_status(u8 sr1, u8 sr2)
{
	if (nor_write_enable())
		return -EIO;

	nor_cs_assert();
	spifc_setup(NOR_CMD_WRSR, 0, 0, 0, 0, 1, 2);
	spifc_write(SFC_DATA, (u32)sr1 | ((u32)sr2 << 8));
	spifc_write(SFC_START, FC_START);
	if (spifc_wait_cmd_end()) {
		nor_cs_deassert();
		return -EIO;
	}
	nor_cs_deassert();
	return nor_wait_wip();
}

/*
 * The long erase/write loops below run synchronously from the network receive
 * path, so nothing pumps the USB device controller while they are busy and the
 * host drops the link mid-transfer.  Override this on boards that need the
 * link to stay up (failsafe.c does it for the ZX297520V3 RNDIS gadget).
 */
__weak void zx_flash_poll(void)
{
}

static int nor_wait_wip(void)
{
	int timeout = 1000000;
	while (--timeout) {
		u8 status = nor_read_status();

		/* Keep the USB/TCP link serviced and watchdog fed during block erase */
		if ((timeout & 0x3F) == 0) {
			zx_flash_poll();
			schedule();
		}

		if (status == 0xFF) {
			udelay(10);
			continue;
		}
		if ((status & 0x01) == 0)
			return 0;
		udelay(10);
	}
	return -ETIMEDOUT;
}

static int nor_read_id(u8 *id_buf, int addr_len, int len)
{
	nor_cs_assert();
	spifc_setup(NOR_CMD_RDID, 0, addr_len, 0, 1, 0, len);
	spifc_write(SFC_START, FC_START);
	int ret = spifc_wait_cmd_end();
	if (ret == 0) {
		if (spifc_read_fifo(id_buf, len))
			ret = -EIO;
	}
	nor_cs_deassert();
	return ret;
}

static int nor_read_data(u32 addr, void *buf, size_t len)
{
	u8 *dst = (u8 *)buf;
	u32 chunks = 0;
	while (len > 0) {
		u32 chunk = len > 64 ? 64 : len;
		if ((++chunks & 0x3F) == 0) {
			zx_flash_poll();
			schedule();
		}
		nor_cs_assert();
		if (g_use_quad)
			spifc_setup_ext(NOR_CMD_FAST_READ_QUAD, addr, 3, 1, 1, 0,
					chunk, 1);
		else
			spifc_setup(NOR_CMD_FAST_READ, addr, 3, 1, 1, 0, chunk);
		spifc_write(SFC_START, FC_START);
		if (spifc_wait_cmd_end()) {
			nor_cs_deassert();
			return -EIO;
		}
		if (spifc_read_fifo(dst, chunk)) {
			nor_cs_deassert();
			return -EIO;
		}
		nor_cs_deassert();
		addr += chunk;
		dst += chunk;
		len -= chunk;
	}
	return 0;
}

static int nor_erase_sector(u32 addr, u8 cmd)
{
	if (nor_write_enable())
		return -EIO;
	nor_cs_assert();
	spifc_setup(cmd, addr, 3, 0, 0, 0, 0);
	spifc_write(SFC_START, FC_START);
	if (spifc_wait_cmd_end()) {
		nor_cs_deassert();
		return -EIO;
	}
	nor_cs_deassert();
	return nor_wait_wip();
}

static int nor_write_page(u32 addr, const u8 *buf, size_t len)
{
	if (nor_write_enable())
		return -EIO;

	nor_cs_assert();
	/*
	 * Quad page program (0x32) when QE is enabled.
	 *
	 * Measured reality, for the record: QSPI buys nothing on the write side --
	 * a 256KB slice is erase 1608ms + program 704ms with only 0.3% of software
	 * overhead, because tBE/tPP are chip-internal and independent of bus width.
	 * Also note the QE self-test only proves the chip -> controller quad READ
	 * direction (0x6B vs 0x0B); the controller -> chip quad WRITE direction is
	 * not covered by it.  Quad write is kept by choice, so a failed program is
	 * currently only detectable via the SR1 P_FAIL/E_FAIL bits.  Single-line
	 * 0x02 stays as the automatic fallback whenever g_use_quad is 0.
	 */
	if (g_use_quad)
		spifc_setup_ext(NOR_CMD_PP_QUAD, addr, 3, 0, 0, 1, len, 1);
	else
		spifc_setup(NOR_CMD_PP, addr, 3, 0, 0, 1, len);

	u32 words = (len + 3) / 4;
	u32 tmp[64];
	const u32 *p;

	if (((uintptr_t)buf & 3) == 0 && (len % 4) == 0) {
		p = (const u32 *)buf;
	} else {
		memset(tmp, 0xFF, sizeof(tmp));
		memcpy(tmp, buf, len);
		p = tmp;
	}

	/* Pre-fill FIFO up to 16 words */
	u32 i = 0;
	while (i < 16 && i < words) {
		spifc_write(SFC_DATA, p[i++]);
	}

	spifc_write(SFC_START, FC_START);

	/* Stream remaining words while checking FIFO space */
	u32 timeout = 500000;
	while (i < words && --timeout) {
		if (((spifc_read(SFC_SW) >> FC_TX_FIFO_CNT_SHIFT) & FC_TX_FIFO_CNT_MASK) > 0) {
			spifc_write(SFC_DATA, p[i++]);
		}
	}

	if (!timeout) {
		printf("[NOR]: write_page FIFO timeout @ 0x%08x (sent %u/%u words)\n", addr, i, words);
		nor_cs_deassert();
		return -ETIMEDOUT;
	}

	if (spifc_wait_cmd_end()) {
		nor_cs_deassert();
		return -EIO;
	}

	nor_cs_deassert();
	return nor_wait_wip();
}

static int nor_erase_range(u32 addr, size_t len)
{
	u32 cur = addr;
	u32 end = addr + len;

	while (cur < end) {
		zx_flash_poll();
		schedule();
		u32 remaining = end - cur;

		/* If 64KB aligned and at least 64KB remaining, use 64KB block erase (fastest) */
		if ((cur & 0xFFFF) == 0 && remaining >= 0x10000) {
			if (nor_erase_sector(cur, NOR_CMD_BE_64K)) {
				printf("NOR 64KB erase error at 0x%08x\n", cur);
				return -EIO;
			}
			cur += 0x10000;
		}
		/* Else if 32KB aligned and at least 32KB remaining, use 32KB block erase */
		else if ((cur & 0x7FFF) == 0 && remaining >= 0x8000) {
			if (nor_erase_sector(cur, NOR_CMD_BE_32K)) {
				printf("NOR 32KB erase error at 0x%08x\n", cur);
				return -EIO;
			}
			cur += 0x8000;
		}
		/* Otherwise use 4KB sector erase to avoid wiping adjacent partition/chunk data */
		else {
			u32 sector_addr = cur & ~0xFFF;
			if (nor_erase_sector(sector_addr, NOR_CMD_SE_4K)) {
				printf("NOR 4KB erase error at 0x%08x\n", sector_addr);
				return -EIO;
			}
			cur += 0x1000;
		}
	}
	return 0;
}

static char g_last_flash_error[128] = "No error";

const char *zx_flash_get_last_error(void)
{
	return g_last_flash_error;
}

static int nor_write_range(u32 addr, const void *buf, size_t len)
{
	const u8 *src = (const u8 *)buf;
	u8 verify_buf[256];

	while (len > 0) {
		u32 page_offset = addr & (g_flash.page_size - 1);
		u32 chunk = g_flash.page_size - page_offset;
		int retry;

		if (chunk > len)
			chunk = len;

		zx_flash_poll();
		schedule();

		for (retry = 0; retry < 2; retry++) {
			if (nor_write_page(addr, src, chunk) == 0 &&
			    nor_read_data(addr, verify_buf, chunk) == 0 &&
			    memcmp(verify_buf, src, chunk) == 0)
				break;
		}

		if (retry == 2) {
			snprintf(g_last_flash_error, sizeof(g_last_flash_error),
				 "NOR write/verify mismatch @ 0x%08X", addr);
			printf("[SPI-NOR]: Write/verify error at 0x%08x\n", addr);
			return -EIO;
		}

		addr += chunk;
		src += chunk;
		len -= chunk;
	}
	return 0;
}

/* ========================================================================= */
/* SPI NAND Flash Implementation                                             */
/* ========================================================================= */

static u8 nand_get_feature(u8 reg_addr)
{
	u8 val = 0xFF;
	spifc_setup(NAND_CMD_GET_FEATURE, reg_addr, 1, 0, 1, 0, 1);
	spifc_write(SFC_START, FC_START);
	if (spifc_wait_cmd_end() == 0)
		spifc_read_fifo(&val, 1);
	return val;
}

static int nand_set_feature(u8 reg_addr, u8 val)
{
	spifc_setup(NAND_CMD_SET_FEATURE, reg_addr, 1, 0, 0, 1, 1);
	if (spifc_write_fifo(&val, 1))
		return -EIO;
	spifc_write(SFC_START, FC_START);
	if (spifc_wait_cmd_end())
		return -EIO;
	return 0;
}

static int nand_wait_oip(void)
{
	int timeout = 500000;
	while ((nand_get_feature(NAND_REG_STATUS) & NAND_STATUS_OIP) && --timeout) {
		if ((timeout & 0x1F) == 0) {
			zx_flash_poll();
			schedule();
		}
	}
	return timeout ? 0 : -ETIMEDOUT;
}

static int nand_write_enable(void)
{
	int retry = 10;
	while (retry-- > 0) {
		spifc_setup(NAND_CMD_WREN, 0, 0, 0, 0, 0, 0);
		spifc_write(SFC_START, FC_START);
		if (spifc_wait_cmd_end() == 0) {
			u8 st = nand_get_feature(NAND_REG_STATUS);
			if (st & NAND_STATUS_WEL)
				return 0;
		}
		udelay(10);
	}
	snprintf(g_last_flash_error, sizeof(g_last_flash_error),
	         "WREN failed (Status=0x%02X)", nand_get_feature(NAND_REG_STATUS));
	printf("[SPI-NAND]: Write Enable (WREN) failed! Status=0x%02X\n",
	       nand_get_feature(NAND_REG_STATUS));
	return -EIO;
}

static void nand_write_disable(void)
{
	spifc_setup(NOR_CMD_WRDI, 0, 0, 0, 0, 0, 0);
	spifc_write(SFC_START, FC_START);
	spifc_wait_cmd_end();
}

static int nand_unlock_blocks(void)
{
	u8 prot = 0;
	nand_set_feature(NAND_REG_PROTECTION, 0x00);
	prot = nand_get_feature(NAND_REG_PROTECTION);
	if (prot != 0x00) {
		nand_write_enable();
		nand_set_feature(NAND_REG_PROTECTION, 0x00);
		prot = nand_get_feature(NAND_REG_PROTECTION);
	}
	if (prot != 0x00) {
		snprintf(g_last_flash_error, sizeof(g_last_flash_error),
		         "Reg 0xA0 locked (0x%02X)", prot);
		printf("[SPI-NAND]: Warning: Protection reg 0xA0 is 0x%02X (LOCKED)!\n", prot);
		return -EACCES;
	}
	return 0;
}

static int nand_read_id(u8 *id_buf)
{
	/* Method 1: Opcode 0x9F with 1-byte address 0x00 (XTX, Winbond, GD, Dosilicon) */
	spifc_setup(NAND_CMD_RDID, 0, 1, 0, 1, 0, 4);
	spifc_write(SFC_START, FC_START);
	if (spifc_wait_cmd_end() == 0 && spifc_read_fifo(id_buf, 4) == 0) {
		if (id_buf[0] != 0x00 && id_buf[0] != 0xFF && id_buf[0] != 0xAA)
			return 0;
	}

	/* Method 2: Opcode 0x9F with 1 dummy byte (Micron, etc.) */
	spifc_setup(NAND_CMD_RDID, 0, 0, 1, 1, 0, 4);
	spifc_write(SFC_START, FC_START);
	if (spifc_wait_cmd_end() == 0 && spifc_read_fifo(id_buf, 4) == 0) {
		if (id_buf[0] != 0x00 && id_buf[0] != 0xFF && id_buf[0] != 0xAA)
			return 0;
	}

	return -ENODEV;
}

static int nand_read_page(u32 page_addr, void *buf)
{
	/* Step 1: Page read into internal on-die cache */
	spifc_setup(NAND_CMD_PAGE_READ, page_addr, 3, 0, 0, 0, 0);
	spifc_write(SFC_START, FC_START);
	if (spifc_wait_cmd_end() || nand_wait_oip())
		return -EIO;

	/* Step 2: Read entire 2048 bytes from cache in one high-speed streamed SPI transaction */
	/* ZTE SPIFC hardware convention for 2048-byte cache read: col |= 0x4000 */
	if (g_use_quad)
		spifc_setup_ext(0x6B, 0x4000, 2, 1, 1, 0, g_flash.page_size, 1);
	else
		spifc_setup(NAND_CMD_READ_CACHE, 0x4000, 2, 1, 1, 0, g_flash.page_size);
	spifc_write(SFC_START, FC_START);

	u32 words = g_flash.page_size / 4;
	u32 timeout = 2000000;
	u32 i = 0;

	if (((uintptr_t)buf & 3) == 0) {
		u32 *p = (u32 *)buf;
		while (i < words && --timeout) {
			if ((spifc_read(SFC_SW) >> FC_RX_FIFO_CNT_SHIFT) & FC_RX_FIFO_CNT_MASK) {
				p[i++] = spifc_read(SFC_DATA);
			}
		}
	} else {
		u32 tmp[512];
		while (i < words && --timeout) {
			if ((spifc_read(SFC_SW) >> FC_RX_FIFO_CNT_SHIFT) & FC_RX_FIFO_CNT_MASK) {
				tmp[i++] = spifc_read(SFC_DATA);
			}
		}
		if (timeout)
			memcpy(buf, tmp, g_flash.page_size);
	}

	if (!timeout) {
		printf("[NAND]: read_page FIFO timeout (got %u/%u words)\n", i, words);
		return -ETIMEDOUT;
	}

	return spifc_wait_cmd_end();
}

static int nand_write_page(u32 page_addr, const void *buf)
{
	u32 tmp[512];
	const u32 *p;
	if (((uintptr_t)buf & 3) == 0) {
		p = (const u32 *)buf;
	} else {
		memcpy(tmp, buf, g_flash.page_size);
		p = tmp;
	}

	if (nand_write_enable())
		return -EIO;

	/* Step 1: Program Load into cache in one full 2048-byte streamed SPI transaction */
	/* ZTE SPIFC convention: col |= 0x4000 for 2048-byte page */
	if (g_use_quad)
		spifc_setup_ext(0x32, 0x4000, 2, 0, 0, 1, g_flash.page_size, 1);
	else
		spifc_setup(NAND_CMD_PROG_LOAD, 0x4000, 2, 0, 0, 1, g_flash.page_size);

	/* Pre-fill FIFO (up to 16 words) before launching SPI transfer */
	u32 words = g_flash.page_size / 4;
	u32 i = 0;
	while (i < 16 && i < words) {
		spifc_write(SFC_DATA, p[i++]);
	}

	spifc_write(SFC_START, FC_START);

	u32 timeout = 2000000;
	while (i < words && --timeout) {
		/* Bits 20:16 of SFC_SW is available free space in TX FIFO */
		if (((spifc_read(SFC_SW) >> FC_TX_FIFO_CNT_SHIFT) & FC_TX_FIFO_CNT_MASK) > 0) {
			spifc_write(SFC_DATA, p[i++]);
		}
	}

	if (!timeout) {
		snprintf(g_last_flash_error, sizeof(g_last_flash_error),
		         "TX FIFO timeout @ page %u (sent %u/%u)", page_addr, i, words);
		nand_write_disable();
		return -ETIMEDOUT;
	}

	if (spifc_wait_cmd_end()) {
		snprintf(g_last_flash_error, sizeof(g_last_flash_error),
		         "PROG_LOAD CMD_END timeout @ page %u", page_addr);
		nand_write_disable();
		return -EIO;
	}

	/* Step 2: Program execute (commit cache to flash array) */
	spifc_setup(NAND_CMD_PROG_EXEC, page_addr, 3, 0, 0, 0, 0);
	spifc_write(SFC_START, FC_START);
	if (spifc_wait_cmd_end()) {
		snprintf(g_last_flash_error, sizeof(g_last_flash_error),
		         "PROG_EXEC CMD_END timeout @ page %u", page_addr);
		nand_write_disable();
		return -EIO;
	}

	if (nand_wait_oip()) {
		snprintf(g_last_flash_error, sizeof(g_last_flash_error),
		         "PROG_EXEC OIP timeout @ page %u (Status=0x%02X)",
		         page_addr, nand_get_feature(NAND_REG_STATUS));
		nand_write_disable();
		return -EIO;
	}

	u8 st = nand_get_feature(NAND_REG_STATUS);
	nand_write_disable();

	if (st & NAND_STATUS_P_FAIL) {
		snprintf(g_last_flash_error, sizeof(g_last_flash_error),
		         "P_FAIL status=0x%02X @ page %u", st, page_addr);
		printf("[SPI-NAND]: write_page failed at page %u, status=0x%02X (P_FAIL)\n",
		       page_addr, st);
		return -EIO;
	}

	return 0;
}

static int nand_erase_block(u32 block_page_addr)
{
	if (nand_write_enable())
		return -EIO;

	spifc_setup(NAND_CMD_BLOCK_ERASE, block_page_addr, 3, 0, 0, 0, 0);
	spifc_write(SFC_START, FC_START);
	if (spifc_wait_cmd_end()) {
		snprintf(g_last_flash_error, sizeof(g_last_flash_error),
		         "Block erase CMD_END timeout @ page %u", block_page_addr);
		nand_write_disable();
		return -EIO;
	}

	if (nand_wait_oip()) {
		snprintf(g_last_flash_error, sizeof(g_last_flash_error),
		         "Block erase OIP timeout @ page %u (Status=0x%02X)",
		         block_page_addr, nand_get_feature(NAND_REG_STATUS));
		nand_write_disable();
		return -EIO;
	}

	u8 st = nand_get_feature(NAND_REG_STATUS);
	nand_write_disable();

	if (st & NAND_STATUS_E_FAIL) {
		snprintf(g_last_flash_error, sizeof(g_last_flash_error),
		         "E_FAIL status=0x%02X @ page %u", st, block_page_addr);
		printf("[SPI-NAND]: Block erase failed at page %u, status=0x%02X (E_FAIL)\n",
		       block_page_addr, st);
		return -EIO;
	}

	return 0;
}

/* ========================================================================= */
/* SPI-NAND Bad Block Management (BBM) & Logical Translation                 */
/* ========================================================================= */

static u8 g_bad_block_bitmap[128]; /* 1024 blocks / 8 = 128 bytes */
static u32 g_bad_block_count = 0;
static int g_bbm_scanned = 0;

void nand_mark_bad(u32 block_idx)
{
	if (block_idx < 1024) {
		if (!((g_bad_block_bitmap[block_idx / 8] >> (block_idx % 8)) & 1)) {
			g_bad_block_bitmap[block_idx / 8] |= (1 << (block_idx % 8));
			g_bad_block_count++;
			printf("[SPI-NAND]: Marked block %u as BAD (total bad: %u)\n",
			       block_idx, g_bad_block_count);
		}
	}
}

int nand_is_bad(u32 block_idx)
{
	if (block_idx >= 1024)
		return 1;
	return (g_bad_block_bitmap[block_idx / 8] >> (block_idx % 8)) & 1;
}

int zx_flash_is_bad_block(u32 block_idx)
{
	return nand_is_bad(block_idx);
}

void zx_flash_mark_bad_block(u32 block_idx)
{
	nand_mark_bad(block_idx);
}

u32 zx_flash_get_bad_block_count(void)
{
	return g_bad_block_count;
}

static int nand_read_oob(u32 page_addr, u8 *oob, u32 len)
{
	/* Step 1: Page read into internal on-die cache */
	spifc_setup(NAND_CMD_PAGE_READ, page_addr, 3, 0, 0, 0, 0);
	spifc_write(SFC_START, FC_START);
	if (spifc_wait_cmd_end() || nand_wait_oip())
		return -EIO;

	/* Step 2: Read OOB from cache. Col = 0x8000 (ZTE SFC convention for OOB) */
	spifc_setup(NAND_CMD_READ_CACHE, 0x8000, 2, 1, 1, 0, len);
	spifc_write(SFC_START, FC_START);
	if (spifc_wait_cmd_end())
		return -EIO;

	return spifc_read_fifo(oob, len);
}

static void nand_scan_bad_blocks(void)
{
	if (g_bbm_scanned)
		return;

	memset(g_bad_block_bitmap, 0, sizeof(g_bad_block_bitmap));
	g_bad_block_count = 0;

	u32 pages_per_block = g_flash.erase_size / g_flash.page_size;
	u32 blk;
	u8 oob[4];

	printf("[SPI-NAND]: Scanning %u blocks for factory bad markers...\n", g_flash.block_count);

	for (blk = 0; blk < g_flash.block_count && blk < 1024; blk++) {
		u32 p0 = blk * pages_per_block;
		u32 p1 = p0 + 1;
		int is_bad = 0;

		if ((blk & 0x07) == 0) {
			zx_flash_poll();
			schedule();
		}

		/* Check Page 0 OOB byte 0 */
		if (nand_read_oob(p0, oob, sizeof(oob)) == 0) {
			if (oob[0] != 0xFF)
				is_bad = 1;
		}

		/* Check Page 1 OOB byte 0 */
		if (!is_bad && nand_read_oob(p1, oob, sizeof(oob)) == 0) {
			if (oob[0] != 0xFF)
				is_bad = 1;
		}

		if (is_bad) {
			nand_mark_bad(blk);
			printf("[SPI-NAND]: Factory BAD block found at block %u (OOB[0]=0x%02X)\n",
			       blk, oob[0]);
		}
	}

	/* Sanity check: SLC NAND usually has at most 20 bad blocks (<2%).
	 * If more than 40 blocks are flagged as bad, the OOB reading command
	 * might have returned invalid bytes (e.g. 0x00).
	 * Fall back to dynamic BBM on erase/write in that case.
	 */
	if (g_bad_block_count > 40) {
		printf("[SPI-NAND]: Warning: OOB scan flagged %u blocks (>40). Disabling OOB BBM to prevent false positives.\n",
		       g_bad_block_count);
		memset(g_bad_block_bitmap, 0, sizeof(g_bad_block_bitmap));
		g_bad_block_count = 0;
	} else {
		printf("[SPI-NAND]: Bad block scan finished. Found %u bad block(s).\n",
		       g_bad_block_count);
	}

	g_bbm_scanned = 1;
}

u32 zx_flash_logical_to_physical(u32 part_base, u32 logical_offset)
{
	if (g_flash.type != FLASH_TYPE_NAND)
		return part_base + logical_offset;

	u32 erase_size = g_flash.erase_size ? g_flash.erase_size : (128 * 1024);
	u32 cur_block = part_base / erase_size;
	u32 remaining = logical_offset;

	while (remaining >= erase_size) {
		if (cur_block >= g_flash.block_count)
			break;
		if (!nand_is_bad(cur_block)) {
			remaining -= erase_size;
		}
		cur_block++;
	}

	while (cur_block < g_flash.block_count && nand_is_bad(cur_block)) {
		cur_block++;
	}

	if (cur_block >= g_flash.block_count) {
		cur_block = g_flash.block_count - 1;
		remaining = 0;
	}

	return (cur_block * erase_size) + remaining;
}

static int nand_erase_range(u32 addr, size_t len)
{
	nand_unlock_blocks();
	u32 start_block = addr / g_flash.erase_size;
	u32 num_good_needed = (len + g_flash.erase_size - 1) / g_flash.erase_size;
	u32 pages_per_block = g_flash.erase_size / g_flash.page_size;
	u32 cur_block = start_block;
	u32 good_erased = 0;

	while (good_erased < num_good_needed) {
		zx_flash_poll();
		schedule();

		if (cur_block >= g_flash.block_count) {
			/* If we reached the end of physical flash and erased available space, stop gracefully */
			if (good_erased > 0 || start_block >= g_flash.block_count - 1) {
				printf("[SPI-NAND]: Erase reached physical flash boundary (%u/%u blocks). Stopping gracefully.\n",
				       good_erased, num_good_needed);
				return 0;
			}
			snprintf(g_last_flash_error, sizeof(g_last_flash_error),
			         "NAND erase ran out of space (block %u)", cur_block);
			printf("[SPI-NAND]: Erase ran out of flash space at block %u\n", cur_block);
			return -ENOSPC;
		}

		if (nand_is_bad(cur_block)) {
			printf("[SPI-NAND]: Skipping bad block %u @ 0x%08X during erase\n",
			       cur_block, cur_block * g_flash.erase_size);
			cur_block++;
			continue;
		}

		u32 block_page = cur_block * pages_per_block;
		if (nand_erase_block(block_page) != 0) {
			/* Erase failed (E_FAIL): mark bad dynamically and skip to next block! */
			printf("[SPI-NAND]: Erase failed at block %u (page %u), marking BAD and skipping...\n",
			       cur_block, block_page);
			nand_mark_bad(cur_block);
			cur_block++;
			continue;
		}

		good_erased++;
		cur_block++;
	}

	return 0;
}

static int nand_write_range(u32 addr, const void *buf, size_t len)
{
	nand_unlock_blocks();
	const u8 *src = (const u8 *)buf;
	const u8 *block_start_src = src;
	size_t block_start_len = len;
	u32 pages_per_block = g_flash.erase_size / g_flash.page_size;
	u32 cur_page = addr / g_flash.page_size;
	u32 tracked_block = cur_page / pages_per_block;
	u8 page_buf[2048];
	u8 verify_buf[2048];

	while (len > 0) {
		u32 cur_block = cur_page / pages_per_block;

		zx_flash_poll();
		schedule();

		/* Skip any bad blocks */
		while (nand_is_bad(cur_block)) {
			printf("[SPI-NAND]: Write skipping bad block %u @ 0x%08X\n",
			       cur_block, cur_block * g_flash.erase_size);
			cur_block++;
			if (cur_block >= g_flash.block_count) {
				printf("[SPI-NAND]: Write reached physical flash boundary at block %u. Stopping gracefully.\n",
				       cur_block);
				return 0;
			}
			cur_page = cur_block * pages_per_block;
		}

		if (cur_block >= g_flash.block_count) {
			printf("[SPI-NAND]: Write reached physical flash boundary at block %u. Stopping gracefully.\n",
			       cur_block);
			return 0;
		}

		/* Checkpoint source pointer at the start of each eraseblock */
		if (cur_block != tracked_block || (cur_page % pages_per_block) == 0) {
			tracked_block = cur_block;
			block_start_src = src;
			block_start_len = len;
		}

		u32 chunk = len > g_flash.page_size ? g_flash.page_size : len;
		const u8 *pdata = src;
		if (chunk < g_flash.page_size) {
			memset(page_buf, 0xFF, g_flash.page_size);
			memcpy(page_buf, src, chunk);
			pdata = page_buf;
		}

		if (nand_write_page(cur_page, pdata) != 0 ||
		    nand_read_page(cur_page, verify_buf) != 0 ||
		    memcmp(verify_buf, pdata, g_flash.page_size) != 0) {
			printf("[SPI-NAND]: Write/verify failed at page %u (block %u), marking BAD and relocating block\n",
			       cur_page, cur_block);
			nand_mark_bad(cur_block);
			/* Rewind to start of current block and relocate to next good block */
			src = block_start_src;
			len = block_start_len;
			cur_block++;
			cur_page = cur_block * pages_per_block;
			continue;
		}

		cur_page++;
		src += chunk;
		len -= chunk;
	}

	return 0;
}

/* ========================================================================= */
/* Generic Flash Detection & Dispatch                                       */
/* ========================================================================= */

struct zx_nor_chip_info {
	u8 id[3];
	const char *name;
	u32 size;
};

/* Official 19 supported SPI NOR flash chips extracted directly from AM206 0.zloader table */
static const struct zx_nor_chip_info s_known_chips[] = {
	{ { 0xC8, 0x60, 0x18 }, "GD25Q128 16MB",    16 * 1024 * 1024 },
	{ { 0xEF, 0x60, 0x18 }, "W25Q128FW 16MB",   16 * 1024 * 1024 },
	{ { 0xF8, 0x42, 0x18 }, "FM25M4AA 16MB",    16 * 1024 * 1024 },
	{ { 0xA1, 0x28, 0x18 }, "FM25W128 16MB",    16 * 1024 * 1024 },
	{ { 0x20, 0x41, 0x17 }, "XM25QU64C 8MB",     8 * 1024 * 1024 },
	{ { 0x20, 0x50, 0x18 }, "XM25QU128 16MB",   16 * 1024 * 1024 },
	{ { 0x20, 0x41, 0x18 }, "XM25QU128C 16MB",  16 * 1024 * 1024 },
	{ { 0x54, 0x60, 0x18 }, "DQ25Q128AL 16MB",  16 * 1024 * 1024 },
	{ { 0xE5, 0x42, 0x18 }, "DS25M4AB 16MB",    16 * 1024 * 1024 },
	{ { 0x1C, 0x78, 0x18 }, "EN25SX128A 16MB",  16 * 1024 * 1024 },
	{ { 0x85, 0x60, 0x17 }, "P25Q64LE 8MB",      8 * 1024 * 1024 },
	{ { 0xEF, 0x60, 0x17 }, "W25Q64JW 8MB",      8 * 1024 * 1024 },
	{ { 0x85, 0x60, 0x18 }, "P25Q128L 16MB",    16 * 1024 * 1024 },
	{ { 0x85, 0x65, 0x18 }, "P25Q128LA 16MB",   16 * 1024 * 1024 },
	{ { 0x25, 0x70, 0x18 }, "SK25LP128 16MB",   16 * 1024 * 1024 },
	{ { 0x1F, 0x42, 0x18 }, "AT25SL128A 16MB",  16 * 1024 * 1024 },
	{ { 0x5E, 0x50, 0x18 }, "ZB25LQ128C 16MB",  16 * 1024 * 1024 },
	{ { 0x68, 0x60, 0x18 }, "BY25FQ128EL 16MB", 16 * 1024 * 1024 },
	{ { 0x5E, 0x50, 0x17 }, "ZB25VQ64 8MB",      8 * 1024 * 1024 },
	{ { 0xEF, 0x40, 0x18 }, "W25Q128BV 16MB",   16 * 1024 * 1024 },
	{ { 0xEF, 0x40, 0x17 }, "W25Q64BV 8MB",      8 * 1024 * 1024 },
	{ { 0xC8, 0x40, 0x18 }, "GD25Q128 16MB",    16 * 1024 * 1024 },
	{ { 0xC8, 0x40, 0x17 }, "GD25Q64 8MB",       8 * 1024 * 1024 },
	{ { 0x68, 0x40, 0x18 }, "BY25Q128 16MB",    16 * 1024 * 1024 },
	{ { 0x68, 0x40, 0x17 }, "BY25Q64 8MB",       8 * 1024 * 1024 },
};

struct zx_nand_chip_info {
	u8 mfr;
	u8 dev;
	const char *name;
	u32 size_mb;
};

static const struct zx_nand_chip_info s_known_nand_chips[] = {
	/* 64MB (512Mbit) SPI NAND chips */
	{ 0x0B, 0x50, "XTX XT26G512 64MB SPI NAND", 64 },
	{ 0x0B, 0xE0, "XTX XT26G512A 64MB SPI NAND", 64 },
	{ 0xEF, 0xA0, "Winbond W25N512GV 64MB SPI NAND", 64 },
	{ 0xC8, 0xB0, "GigaDevice GD5F512M 64MB SPI NAND", 64 },
	{ 0xC8, 0xD0, "GigaDevice GD5F512M 64MB SPI NAND", 64 },
	{ 0xC8, 0xF0, "GigaDevice GD5F512M 64MB SPI NAND", 64 },
	{ 0xE5, 0x70, "Dosilicon DS35Q512M 64MB SPI NAND", 64 },
	{ 0xBA, 0x20, "Zetta ZD35Q512 64MB SPI NAND", 64 },
	{ 0xCD, 0x70, "Foresee F35SQA512M 64MB SPI NAND", 64 },
	/* 128MB (1Gbit) & 256MB (2Gbit) SPI NAND chips */
	{ 0x0B, 0x51, "XTX XT26G01A 128MB SPI NAND", 128 },
	{ 0x0B, 0x52, "XTX XT26G02A 256MB SPI NAND", 256 },
	{ 0x0B, 0x53, "XTX XT26G04A 512MB SPI NAND", 512 },
	{ 0x0B, 0x31, "XTX XT26G01B 128MB SPI NAND", 128 },
	{ 0x0B, 0x32, "XTX XT26G02B 256MB SPI NAND", 256 },
	{ 0x0B, 0xE1, "XTX XT26G01A 128MB SPI NAND", 128 },
	{ 0x0B, 0xE2, "XTX XT26G02A 256MB SPI NAND", 256 },
	{ 0xEF, 0xAA, "Winbond W25N01GV 128MB SPI NAND", 128 },
	{ 0xEF, 0xBA, "Winbond W25N01GW 128MB SPI NAND", 128 },
	{ 0xEF, 0xBB, "Winbond W25N02KV 256MB SPI NAND", 256 },
	{ 0xC8, 0xB1, "GigaDevice GD5F1GQ4 128MB SPI NAND", 128 },
	{ 0xC8, 0xD1, "GigaDevice GD5F1GQ5 128MB SPI NAND", 128 },
	{ 0xC8, 0xB2, "GigaDevice GD5F2GQ4 256MB SPI NAND", 256 },
	{ 0xC8, 0xD2, "GigaDevice GD5F2GQ5 256MB SPI NAND", 256 },
	{ 0xE5, 0x71, "Dosilicon DS35Q1GA 128MB SPI NAND", 128 },
	{ 0xE5, 0x72, "Dosilicon DS35Q2GA 256MB SPI NAND", 256 },
	{ 0xCD, 0x12, "Foresee F35SQA001G 128MB SPI NAND", 128 },
	{ 0xCD, 0x14, "Foresee F35SQA002G 256MB SPI NAND", 256 },
	{ 0x2C, 0x24, "Micron MT29F1G01 128MB SPI NAND", 128 },
	{ 0x2C, 0x34, "Micron MT29F2G01 256MB SPI NAND", 256 },
};

struct nor_probe_cand {
	int use_gpio86;
	u32 clk_div;
	int addr_len;
	const char *desc;
};

static const struct nor_probe_cand s_candidates[] = {
	{ 0, 2, 0, "HW CS (GPIO93) @ 124.8MHz" },
	{ 0, 7, 0, "HW CS (GPIO93) @ ~45MHz" },
	{ 0, 6, 0, "HW CS (GPIO93) @ ~31MHz" },
	{ 1, 2, 0, "GPIO86 CS @ 124.8MHz" },
	{ 1, 7, 0, "GPIO86 CS @ ~45MHz" },
};

static void zx_flash_test_and_enable_quad(void)
{
	g_use_quad = 0;

	if (g_flash.type == FLASH_TYPE_NAND) {
		/* Step 1: Check and set QE (Quad Enable) bit in Feature Register 0xB0 */
		u8 cfg = nand_get_feature(NAND_REG_CONFIG);
		if (!(cfg & 0x01)) {
			nand_write_enable();
			nand_set_feature(NAND_REG_CONFIG, cfg | 0x01);
			cfg = nand_get_feature(NAND_REG_CONFIG);
		}

		/* Step 2: Test Quad Read vs Single Read on Page 0 */
		u8 single_buf[16] = {0};
		u8 quad_buf[16] = {0};

		/* Read Page 0 into cache */
		spifc_setup(NAND_CMD_PAGE_READ, 0, 3, 0, 0, 0, 0);
		spifc_write(SFC_START, FC_START);
		if (spifc_wait_cmd_end() || nand_wait_oip())
			return;

		/* Read 16 bytes via Single SPI (Opcode 0x03) */
		spifc_setup(NAND_CMD_READ_CACHE, 0x4000, 2, 1, 1, 0, 16);
		spifc_write(SFC_START, FC_START);
		if (spifc_wait_cmd_end() || spifc_read_fifo(single_buf, 16))
			return;

		/* Read 16 bytes via Quad SPI (Opcode 0x6B, TRANS_MOD=5) */
		spifc_setup_ext(0x6B, 0x4000, 2, 1, 1, 0, 16, 1);
		spifc_write(SFC_START, FC_START);
		if (spifc_wait_cmd_end() || spifc_read_fifo(quad_buf, 16))
			return;

		/* Compare */
		if (memcmp(single_buf, quad_buf, 16) == 0) {
			g_use_quad = 1;
			snprintf(g_quad_reason, sizeof(g_quad_reason),
				 "QE set, self-test OK (0xB0=0x%02X)", cfg);
			printf("[SPI-NAND]: 🚀 Quad SPI (4-line) verified & ENABLED! (Reg 0xB0=0x%02X)\n", cfg);
		} else {
			snprintf(g_quad_reason, sizeof(g_quad_reason),
				 "self-test mismatch S=%02X%02X Q=%02X%02X",
				 single_buf[0], single_buf[1], quad_buf[0], quad_buf[1]);
			printf("[SPI-NAND]: Quad SPI self-test mismatch, keeping Single SPI mode\n"
			       "            Single: %02X %02X %02X %02X\n"
			       "            Quad:   %02X %02X %02X %02X\n",
			       single_buf[0], single_buf[1], single_buf[2], single_buf[3],
			       quad_buf[0], quad_buf[1], quad_buf[2], quad_buf[3]);
		}
	} else {
		/*
		 * SPI NOR: set the Quad Enable (QE, SR2 bit1), then verify a quad
		 * read against a single-line read before trusting it.  QE only
		 * enables the 4-line *option* -- single-line read commands keep
		 * working regardless -- so a mismatch just falls back to Single SPI
		 * instead of making the chip unreachable.
		 */
		u8 sr1 = nor_read_status();
		u8 sr2 = nor_read_status2();
		int qe_ok = (sr2 & 0x02) != 0;

		if (!qe_ok) {
			/* Refuse to touch SR1 if it looks unreadable -- a bad WRSR
			 * could set block-protection bits and lock the flash. */
			if (sr1 == 0xFF) {
				snprintf(g_quad_reason, sizeof(g_quad_reason),
					 "QE off & SR1 unreadable (0xFF)");
				printf("[SPI-NOR]: SR1 reads 0xFF, refusing WRSR; keeping Single SPI\n");
				g_use_quad = 0;
				return;
			}
			if (nor_write_status(sr1 & ~0x01, sr2 | 0x02) == 0)
				sr2 = nor_read_status2();
			qe_ok = (sr2 & 0x02) != 0;
		}

		if (!qe_ok) {
			snprintf(g_quad_reason, sizeof(g_quad_reason),
				 "QE not set (SR2=0x%02X)", sr2);
			printf("[SPI-NOR]: QE bit not set (SR2=0x%02X), keeping Single SPI\n", sr2);
			g_use_quad = 0;
			return;
		}

		u8 single_buf[16] = {0};
		u8 quad_buf[16] = {0};

		/* Single-line read (0x0B), then quad read (0x6B, TRANS_MOD=5) */
		if (nor_read_data(0x4000, single_buf, 16) == 0) {
			nor_cs_assert();
			spifc_setup_ext(NOR_CMD_FAST_READ_QUAD, 0x4000, 3, 1, 1, 0, 16, 1);
			spifc_write(SFC_START, FC_START);
			if (spifc_wait_cmd_end() == 0)
				spifc_read_fifo(quad_buf, 16);
			nor_cs_deassert();
		}

		if (memcmp(single_buf, quad_buf, 16) == 0) {
			g_use_quad = 1;
			snprintf(g_quad_reason, sizeof(g_quad_reason),
				 "QE set, self-test OK (SR2=0x%02X)", sr2);
			printf("[SPI-NOR]: Quad SPI (4-line) verified & ENABLED! (SR2=0x%02X)\n", sr2);
		} else {
			g_use_quad = 0;
			snprintf(g_quad_reason, sizeof(g_quad_reason),
				 "self-test mismatch S=%02X%02X Q=%02X%02X",
				 single_buf[0], single_buf[1], quad_buf[0], quad_buf[1]);
			printf("[SPI-NOR]: Quad SPI self-test mismatch, keeping Single SPI\n"
			       "           Single: %02X %02X %02X %02X\n"
			       "           Quad:   %02X %02X %02X %02X\n",
			       single_buf[0], single_buf[1], single_buf[2], single_buf[3],
			       quad_buf[0], quad_buf[1], quad_buf[2], quad_buf[3]);
		}
	}
}

int zx_flash_init(void)
{
	u8 id[8] = {0};
	int ret, i;
	int found_cand = -1;

	u8 last_cand_id[8] = {0};

	if (g_flash.initialized)
		return 0;

	spifc_hwinit();

	u32 chip_id_reg = readl((void __iomem *)0x0121b040);
	u32 soc_id = chip_id_reg >> 8;
	printf("[ZX_FLASH]: ChipID 0x0121b040 = 0x%08X (SocID: 0x%06X)\n", chip_id_reg, soc_id);

	/* Probe 1: Multi-candidate Auto Probe for SPI NOR */
	for (i = 0; i < ARRAY_SIZE(s_candidates); i++) {
		spifc_set_cs_mode(s_candidates[i].use_gpio86);
		spifc_set_clock(s_candidates[i].clk_div);
		nor_reset_controller();

		u8 cand_id[8] = {0};
		ret = nor_read_id(cand_id, s_candidates[i].addr_len, 8);
		printf("[ZX_FLASH]: Candidate %d [%s] ret=%d, ID=[0x%02X 0x%02X 0x%02X 0x%02X]\n",
		       i, s_candidates[i].desc, ret, cand_id[0], cand_id[1], cand_id[2], cand_id[3]);

		memcpy(last_cand_id, cand_id, sizeof(last_cand_id));

		if (ret == 0 && cand_id[0] != 0x00 && cand_id[0] != 0xFF && cand_id[0] != 0xAA) {
			found_cand = i;
			memcpy(id, cand_id, 3);
			break;
		}
	}

	if (found_cand >= 0) {
		/* Valid SPI NOR Flash ID detected */
		g_flash.type = FLASH_TYPE_NOR;
		g_flash.manuf_id = id[0];
		g_flash.chip_id = (id[1] << 8) | id[2];
		g_flash.page_size = 256;
		g_flash.erase_size = 64 * 1024; /* 64KB block */

		const char *found_name = NULL;
		for (int c = 0; c < ARRAY_SIZE(s_known_chips); c++) {
			if (s_known_chips[c].id[0] == id[0] &&
			    s_known_chips[c].id[1] == id[1] &&
			    s_known_chips[c].id[2] == id[2]) {
				found_name = s_known_chips[c].name;
				g_flash.total_size = s_known_chips[c].size;
				break;
			}
		}

		if (found_name) {
			snprintf(g_flash.model, sizeof(g_flash.model), "%s", found_name);
		} else if (id[2] == 0x18) {
			g_flash.total_size = 16 * 1024 * 1024;
			snprintf(g_flash.model, sizeof(g_flash.model), "SPI NOR 16MB [0x%02X%04X]", id[0], g_flash.chip_id);
		} else if (id[2] == 0x17) {
			g_flash.total_size = 8 * 1024 * 1024;
			snprintf(g_flash.model, sizeof(g_flash.model), "SPI NOR 8MB [0x%02X%04X]", id[0], g_flash.chip_id);
		} else if (id[2] >= 0x14 && id[2] <= 0x1A) {
			g_flash.total_size = 1U << id[2];
			snprintf(g_flash.model, sizeof(g_flash.model), "SPI NOR %uMB [0x%02X%04X]",
			         g_flash.total_size / (1024 * 1024), id[0], g_flash.chip_id);
		} else {
			g_flash.total_size = 8 * 1024 * 1024;
			snprintf(g_flash.model, sizeof(g_flash.model), "SPI NOR %uMB",
			         g_flash.total_size / (1024 * 1024));
		}

		g_flash.block_count = g_flash.total_size / g_flash.erase_size;
		g_flash.initialized = 1;

		snprintf(g_flash_probe_diag, sizeof(g_flash_probe_diag),
		         "Cand: %s | ID: 0x%02X%04X",
		         s_candidates[found_cand].desc, g_flash.manuf_id, g_flash.chip_id);

		/* Switch back to high-speed 124.8MHz */
		spifc_set_clock(2);

		printf("Flash: %s [ID: 0x%02X 0x%04X, Size: %u MB] (Probe: %s)\n",
		       g_flash.model, g_flash.manuf_id, g_flash.chip_id,
		       g_flash.total_size / (1024 * 1024), s_candidates[found_cand].desc);

		u8 tbuf[16];
		u32 scan_addrs[] = {
			0x000000, /* zloader header */
			0x000010, /* zloader zeros */
			0x000020, /* zloader zeros */
			0x000040, /* zloader */
			0x000080, /* zloader */
			0x000800, /* zloader body */
			0x001000, /* zloader body */
			0x008000, /* nvrofs */
			0x040000, /* uboot partition */
			0x070000, /* uboot-mirr (16M) */
			0x0A0000, /* imagefs (16M AM206) */
			0x068000, /* imagefs (8M default) */
			0x620000, /* rootfs (16M AM206) */
			0x468000, /* rootfs (8M default) */
		};
		const char *names[] = {
			"zloader hdr", "zloader +10", "zloader +20", "zloader +40",
			"zloader +80", "zloader 2KB", "zloader 4KB", "nvrofs 32KB",
			"uboot 256KB", "uboot-mirr",  "imagefs(16M)", "imagefs(8M)",
			"rootfs(16M)", "rootfs(8M)",
		};
		int s;

		printf("\n[FLASH_SCAN]: Multi-partition data inspection:\n");
		for (s = 0; s < ARRAY_SIZE(scan_addrs); s++) {
			memset(tbuf, 0xAA, sizeof(tbuf));
			int err = nor_read_data(scan_addrs[s], tbuf, sizeof(tbuf));
			printf("  %-12s @ 0x%06X [%s]: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
			       names[s], scan_addrs[s], err ? "ERR" : "OK ",
			       tbuf[0], tbuf[1], tbuf[2], tbuf[3], tbuf[4], tbuf[5], tbuf[6], tbuf[7],
			       tbuf[8], tbuf[9], tbuf[10], tbuf[11], tbuf[12], tbuf[13], tbuf[14], tbuf[15]);
		}

		goto probe_parts;
	}

	/* Probe 2: Try SPI NAND ID query (0x9F with 1-byte addr or 1 dummy byte) */
	spifc_set_cs_mode(0);
	spifc_set_clock(2);
	nor_reset_controller();
	memset(id, 0, sizeof(id));
	ret = nand_read_id(id);
	printf("[ZX_FLASH]: Probe SPI NAND RDID ret=%d, ID=[0x%02X 0x%02X 0x%02X 0x%02X]\n",
	       ret, id[0], id[1], id[2], id[3]);

	if (ret == 0 && id[0] != 0x00 && id[0] != 0xFF && id[0] != 0xAA) {
		g_flash.type = FLASH_TYPE_NAND;
		g_flash.manuf_id = id[0];
		g_flash.chip_id = (id[1] << 8) | id[2];
		g_flash.page_size = 2048;
		g_flash.erase_size = 128 * 1024; /* 128KB block */
		g_flash.total_size = 128 * 1024 * 1024; /* 128MB default NAND */
		g_flash.block_count = g_flash.total_size / g_flash.erase_size;

		const char *nand_name = NULL;
		for (int c = 0; c < ARRAY_SIZE(s_known_nand_chips); c++) {
			if (s_known_nand_chips[c].mfr == id[0] &&
			    s_known_nand_chips[c].dev == id[1]) {
				nand_name = s_known_nand_chips[c].name;
				g_flash.total_size = s_known_nand_chips[c].size_mb * 1024 * 1024;
				g_flash.block_count = g_flash.total_size / g_flash.erase_size;
				break;
			}
		}

		if (nand_name) {
			snprintf(g_flash.model, sizeof(g_flash.model), "%s", nand_name);
		} else {
			snprintf(g_flash.model, sizeof(g_flash.model), "SPI NAND %uMB [0x%02X%04X]",
			         g_flash.total_size / (1024 * 1024), id[0], g_flash.chip_id);
		}

		snprintf(g_flash_probe_diag, sizeof(g_flash_probe_diag),
		         "SocID: 0x%06X | %s | ID: 0x%02X%04X",
		         soc_id, g_flash.model, g_flash.manuf_id, g_flash.chip_id);

		/* Unlock NAND blocks */
		nand_unlock_blocks();

		g_flash.initialized = 1;
		printf("Flash: %s [ID: 0x%02X 0x%04X, Size: %u MB]\n",
		       g_flash.model, g_flash.manuf_id, g_flash.chip_id,
		       g_flash.total_size / (1024 * 1024));
		nand_scan_bad_blocks();
		goto probe_parts;
	}

	/* Probe Failed: Flash did NOT respond with valid JEDEC ID. Do NOT fake fallback! */
	g_flash.type = FLASH_TYPE_UNKNOWN;
	g_flash.total_size = 0;
	g_flash.page_size = 0;
	g_flash.erase_size = 0;
	g_flash.block_count = 0;
	g_flash.manuf_id = last_cand_id[0];
	g_flash.chip_id = (last_cand_id[1] << 8) | last_cand_id[2];
	g_flash.initialized = 1;
	snprintf(g_flash.model, sizeof(g_flash.model), "未识别到闪存 (探测失败)");
	snprintf(g_flash_probe_diag, sizeof(g_flash_probe_diag),
	         "SocID: 0x%06X | 读出ID: [0x%02X 0x%02X 0x%02X 0x%02X] - 硬件未响应",
	         soc_id, last_cand_id[0], last_cand_id[1], last_cand_id[2], last_cand_id[3]);

	printf("[ZX_FLASH]: ❌ Flash probe failed! (Last ID: 0x%02X 0x%02X 0x%02X 0x%02X)\n",
	       last_cand_id[0], last_cand_id[1], last_cand_id[2], last_cand_id[3]);
	return -ENODEV;

probe_parts:
	zx_flash_test_and_enable_quad();
	zx_flash_probe_partition_table();
	return 0;
}

int zx_flash_get_type(void)
{
	if (!g_flash.initialized)
		zx_flash_init();
	return g_flash.type;
}

u32 zx_flash_get_size(void)
{
	if (!g_flash.initialized)
		zx_flash_init();
	return g_flash.total_size;
}

const char *zx_flash_get_name(void)
{
	if (!g_flash.initialized)
		zx_flash_init();
	return g_flash.model;
}

const char *zx_flash_get_diag_str(void)
{
	return g_flash_probe_diag;
}

int zx_flash_set_profile(u32 size_mb)
{
	if (size_mb != 8 && size_mb != 16 && size_mb != 32 &&
	    size_mb != 64 && size_mb != 128)
		return -EINVAL;

	if (size_mb == 64 || size_mb == 128) {
		g_flash.type = FLASH_TYPE_NAND;
		g_flash.total_size = size_mb * 1024 * 1024;
		g_flash.page_size = 2048;
		g_flash.erase_size = 128 * 1024;
		g_flash.block_count = g_flash.total_size / g_flash.erase_size;
		g_flash.manuf_id = 0x0B;
		g_flash.chip_id = (size_mb == 64) ? 0x5000 : 0x5100;
		snprintf(g_flash.model, sizeof(g_flash.model),
			 "SPI NAND %uMB (手动设置)", size_mb);
		g_bbm_scanned = 0;
		nand_unlock_blocks();
		nand_scan_bad_blocks();
	} else {
		g_flash.type = FLASH_TYPE_NOR;
		g_flash.total_size = size_mb * 1024 * 1024;
		g_flash.page_size = 256;
		g_flash.erase_size = 64 * 1024;
		g_flash.block_count = g_flash.total_size / g_flash.erase_size;
		g_flash.manuf_id = 0x5E;
		g_flash.chip_id = (size_mb == 16) ? 0x5018 : 0x5017;
		snprintf(g_flash.model, sizeof(g_flash.model),
			 "SPI NOR %uMB (手动设置)", size_mb);
	}
	g_flash.initialized = 1;

	snprintf(g_flash_probe_diag, sizeof(g_flash_probe_diag),
		 "手动配置 %uMB %s 模式", size_mb,
		 (g_flash.type == FLASH_TYPE_NAND) ? "NAND" : "NOR");
	zx_flash_test_and_enable_quad();
	zx_flash_probe_partition_table();
	printf("[ZX_FLASH]: Manually switched to %s\n", g_flash.model);
	return 0;
}

int zx_flash_get_info_str(char *buf, size_t maxlen)
{
	if (!g_flash.initialized)
		zx_flash_init();

	if (g_flash.type == FLASH_TYPE_UNKNOWN || g_flash.total_size == 0) {
		snprintf(buf, maxlen, "【❌ 闪存探测失败】%s", g_flash_probe_diag);
		return 0;
	}

	if (g_flash.type == FLASH_TYPE_NAND) {
		snprintf(buf, maxlen, "%s | ID: 0x%02X%04X | Size: %u MB | 坏块: %u | %s | %s",
			 g_flash.model, g_flash.manuf_id, g_flash.chip_id,
			 g_flash.total_size / (1024 * 1024),
			 g_bad_block_count,
			 g_use_quad ? "Quad SPI (4线加速)" : "Single SPI",
			 g_flash_probe_diag);
	} else {
		snprintf(buf, maxlen, "%s | ID: 0x%02X%04X | Size: %u MB | %s (%s) | %s",
			 g_flash.model, g_flash.manuf_id, g_flash.chip_id,
			 g_flash.total_size / (1024 * 1024),
			 g_use_quad ? "Quad SPI (4线加速)" : "Single SPI",
			 g_quad_reason,
			 g_flash_probe_diag);
	}
	return 0;
}

int zx_flash_read(u32 addr, void *buf, size_t len)
{
	if (!g_flash.initialized)
		zx_flash_init();

	if (g_flash.type == FLASH_TYPE_NOR) {
		return nor_read_data(addr, buf, len);
	} else if (g_flash.type == FLASH_TYPE_NAND) {
		u32 pages_per_block = g_flash.erase_size / g_flash.page_size;
		u32 page = addr / g_flash.page_size;
		u32 offset = addr % g_flash.page_size;
		u8 page_buf[2048];
		u8 *p = (u8 *)buf;
		size_t remaining = len;

		while (remaining > 0) {
			size_t chunk = g_flash.page_size - offset;
			if (chunk > remaining)
				chunk = remaining;

			if ((page & 0x07) == 0) {
				zx_flash_poll();
				schedule();
			}

			u32 blk = page / pages_per_block;
			if (nand_is_bad(blk)) {
				/* Bad block: fill 0xFF to allow streaming backup to proceed */
				memset(p, 0xFF, chunk);
			} else {
				if (offset == 0 && chunk == g_flash.page_size) {
					if (nand_read_page(page, p) < 0)
						memset(p, 0xFF, chunk);
				} else {
					if (nand_read_page(page, page_buf) < 0) {
						memset(p, 0xFF, chunk);
					} else {
						memcpy(p, page_buf + offset, chunk);
					}
				}
			}
			p += chunk;
			remaining -= chunk;
			page++;
			offset = 0;
		}
		return 0;
	}

	return -ENODEV;
}

int zx_flash_erase(u32 addr, size_t len)
{
	if (!g_flash.initialized)
		zx_flash_init();

	if (g_flash.type == FLASH_TYPE_NOR)
		return nor_erase_range(addr, len);
	else
		return nand_erase_range(addr, len);
}

int zx_flash_write(u32 addr, const void *buf, size_t len)
{
	if (!g_flash.initialized)
		zx_flash_init();

	if (g_flash.type == FLASH_TYPE_NOR)
		return nor_write_range(addr, buf, len);
	else
		return nand_write_range(addr, buf, len);
}

/* ========================================================================= */
/* Failsafe Firmware & Partition Flashing Interface                         */
/* ========================================================================= */

/* ========================================================================= */
/* Partition Table Structures & Dynamic Management                          */
/* ========================================================================= */

#define ZTE_PTABLE_MAGIC    0x31594876
#define MAX_DYN_PARTS       24

struct zx_ptable_entry {
	u8 part_name[16];
	u8 part_type[16];
	u32 part_offset;
	u32 part_size;
};

struct zx_ptable_hdr {
	u32 magic;
	u8 platform_name[16];
	u32 version;
	u32 entrys;
	u32 crc;
	struct zx_ptable_entry table[30];
};

struct zx_part_def {
	char name[16];
	u32 offset;
	u32 max_size;
	bool is_full;
};

static struct zx_part_def g_dyn_parts[MAX_DYN_PARTS];
static u32 g_dyn_part_count = 0;
static int g_has_ptable = 0;

static const struct zx_part_def g_nor_parts_8m[] = {
	{ "zloader",   0x00000000, 0x00008000, false }, /* 32KB Stage 1 */
	{ "nvrofs",    0x00008000, 0x00038000, false }, /* 224KB NV calibration params */
	{ "uboot",     0x00040000, 0x00028000, false }, /* U-Boot (160KB/192KB) */
	{ "imagefs",   0x00068000, 0x00400000, false }, /* Kernel/System image (4MB) */
	{ "rootfs",    0x00468000, 0x00318000, false }, /* Root filesystem (3168KB) */
	{ "userdata",  0x00780000, 0x00080000, false }, /* Userdata (512KB) */
	{ "full",      0x00000000, 0x00800000, true  }, /* Full Flash (8MB) */
};

static const struct zx_part_def g_nor_parts_16m[] = {
	{ "zloader",    0x00000000, 0x00008000, false }, /* 32KB Stage 1 */
	{ "nvrofs",     0x00008000, 0x00038000, false }, /* 224KB NV calibration params */
	{ "uboot",      0x00040000, 0x00030000, false }, /* 192KB U-Boot */
	{ "uboot-mirr", 0x00070000, 0x00030000, false }, /* 192KB U-Boot mirror backup */
	{ "imagefs",    0x000A0000, 0x00580000, false }, /* 5.5MB Kernel / System */
	{ "rootfs",     0x00620000, 0x00780000, false }, /* 7.5MB Root filesystem */
	{ "resource",   0x00DA0000, 0x00040000, false }, /* 256KB Resource */
	{ "userdata",   0x00DE0000, 0x00220000, false }, /* 2176KB Userdata */
	{ "full",       0x00000000, 0x01000000, true  }, /* 16MB Full Flash */
};

static const struct zx_part_def g_nand_parts_64m[] = {
	{ "zloader",   0x00000000, 0x00020000, false }, /* 128KB Stage 1 */
	{ "uboot",     0x00020000, 0x00180000, false }, /* 1.5MB Stage 2 U-Boot */
	{ "nvrofs",    0x00220000, 0x00200000, false }, /* 2MB NV parameters */
	{ "imagefs",   0x00420000, 0x01C00000, false }, /* 28MB Kernel image */
	{ "rootfs",    0x02020000, 0x01FC0000, false }, /* 31.75MB Root filesystem */
	{ "userdata",  0x03FE0000, 0x00020000, false }, /* 128KB Userdata */
	{ "full",      0x00000000, 0x04000000, true  }, /* 64MB Full NAND */
};

static const struct zx_part_def g_nand_parts[] = {
	{ "zloader",   0x00000000, 0x00020000, false }, /* 128KB Stage 1 */
	{ "uboot",     0x00020000, 0x00180000, false }, /* 1.5MB Stage 2 U-Boot */
	{ "nvrofs",    0x00220000, 0x00200000, false }, /* 2MB NV parameters */
	{ "imagefs",   0x00420000, 0x04000000, false }, /* 64MB Kernel image */
	{ "rootfs",    0x04420000, 0x03BC0000, false }, /* 59.75MB Root filesystem */
	{ "userdata",  0x07FE0000, 0x00020000, false }, /* 128KB Userdata */
	{ "full",      0x00000000, 0x08000000, true  }, /* 128MB Full NAND */
};

static void zx_flash_probe_partition_table(void)
{
	struct zx_ptable_hdr *hdr;
	u8 buf[2048];
	u32 offsets[] = { 0x2000, 0x8000, 0x0, 0x20000, 0x40000 };
	int i, j;

	g_has_ptable = 0;
	g_dyn_part_count = 0;

	for (i = 0; i < ARRAY_SIZE(offsets); i++) {
		if (zx_flash_read(offsets[i], buf, sizeof(buf)) < 0)
			continue;

		hdr = (struct zx_ptable_hdr *)buf;
		if (hdr->magic == ZTE_PTABLE_MAGIC && hdr->entrys > 0 && hdr->entrys <= 30) {
			u32 raw_size = 0;
			int table_fits = 1;

			for (j = 0; j < hdr->entrys; j++) {
				u32 po = hdr->table[j].part_offset;
				u32 ps = hdr->table[j].part_size;
				const char *pn = (const char *)hdr->table[j].part_name;

				if (ps == 0xFFFFFFFF || ps == 0 || !strcmp(pn, "ddr"))
					continue;
				if (!strcmp(pn, "raw")) {
					raw_size = ps;
					continue;
				}
				if (g_flash.total_size && (po >= g_flash.total_size || po + ps > g_flash.total_size)) {
					table_fits = 0;
					break;
				}
			}

			/* Ignore NOR partition table when operating in NAND profile (and vice versa) */
			if (!table_fits ||
			    (g_flash.type == FLASH_TYPE_NAND && raw_size > 0 && raw_size <= 16 * 1024 * 1024)) {
				printf("[ZX_FLASH]: On-flash partition table @ 0x%04X does not match %uMB %s profile, using profile layout\n",
				       offsets[i], g_flash.total_size / (1024 * 1024),
				       (g_flash.type == FLASH_TYPE_NAND) ? "NAND" : "NOR");
				break;
			}

			g_has_ptable = 1;
			printf("[ZX_FLASH]: Found ZTE Partition Table @ 0x%04X (Platform: %s, %u entries)\n",
			       offsets[i], hdr->platform_name, hdr->entrys);

			g_dyn_part_count = 0;
			for (j = 0; j < hdr->entrys && g_dyn_part_count < MAX_DYN_PARTS; j++) {
				/* Skip DDR or invalid entries */
				if (hdr->table[j].part_size == 0xFFFFFFFF ||
				    hdr->table[j].part_size == 0 ||
				    !strcmp((char *)hdr->table[j].part_name, "ddr"))
					continue;

				strncpy(g_dyn_parts[g_dyn_part_count].name, (char *)hdr->table[j].part_name, 15);
				g_dyn_parts[g_dyn_part_count].name[15] = '\0';
				g_dyn_parts[g_dyn_part_count].offset = hdr->table[j].part_offset;
				g_dyn_parts[g_dyn_part_count].max_size = hdr->table[j].part_size;
				g_dyn_parts[g_dyn_part_count].is_full = !strcmp(g_dyn_parts[g_dyn_part_count].name, "raw");
				if (g_dyn_parts[g_dyn_part_count].is_full && g_flash.total_size)
					g_dyn_parts[g_dyn_part_count].max_size = g_flash.total_size;
				printf("  Part[%02d]: %-12s @ 0x%08X (size: 0x%08X, %u KB)\n",
				       g_dyn_part_count, g_dyn_parts[g_dyn_part_count].name,
				       g_dyn_parts[g_dyn_part_count].offset, g_dyn_parts[g_dyn_part_count].max_size,
				       g_dyn_parts[g_dyn_part_count].max_size / 1024);
				g_dyn_part_count++;
			}
			return;
		}
	}

	printf("[ZX_FLASH]: No ZTE partition table detected on Flash, using default profile\n");
}

int zx_flash_get_part_count(void)
{
	if (!g_flash.initialized)
		zx_flash_init();
	if (g_flash.type == FLASH_TYPE_UNKNOWN || g_flash.total_size == 0)
		return 0;
	if (g_has_ptable && g_dyn_part_count > 0)
		return g_dyn_part_count;
	if (g_flash.type == FLASH_TYPE_NAND) {
		if (g_flash.total_size <= 64 * 1024 * 1024)
			return ARRAY_SIZE(g_nand_parts_64m);
		return ARRAY_SIZE(g_nand_parts);
	}
	if (g_flash.total_size >= 16 * 1024 * 1024)
		return ARRAY_SIZE(g_nor_parts_16m);
	return ARRAY_SIZE(g_nor_parts_8m);
}

int zx_flash_has_partition_table(void)
{
	if (!g_flash.initialized)
		zx_flash_init();
	return g_has_ptable;
}

int zx_flash_get_partition_info(u32 idx, char *name, u32 *offset, u32 *size)
{
	if (!g_flash.initialized)
		zx_flash_init();

	if (g_flash.type == FLASH_TYPE_UNKNOWN || g_flash.total_size == 0)
		return -ENODEV;

	if (g_has_ptable && g_dyn_part_count > 0) {
		if (idx >= g_dyn_part_count)
			return -EINVAL;
		if (name)
			strcpy(name, g_dyn_parts[idx].name);
		if (offset)
			*offset = g_dyn_parts[idx].offset;
		if (size)
			*size = g_dyn_parts[idx].max_size;
		return 0;
	}

	const struct zx_part_def *parts = g_nor_parts_8m;
	u32 count = ARRAY_SIZE(g_nor_parts_8m);

	if (g_flash.type == FLASH_TYPE_NAND) {
		if (g_flash.total_size <= 64 * 1024 * 1024) {
			parts = g_nand_parts_64m;
			count = ARRAY_SIZE(g_nand_parts_64m);
		} else {
			parts = g_nand_parts;
			count = ARRAY_SIZE(g_nand_parts);
		}
	} else if (g_flash.total_size >= 16 * 1024 * 1024) {
		parts = g_nor_parts_16m;
		count = ARRAY_SIZE(g_nor_parts_16m);
	}

	if (idx >= count)
		return -EINVAL;

	if (name)
		strcpy(name, parts[idx].name);
	if (offset)
		*offset = parts[idx].offset;
	if (size)
		*size = parts[idx].max_size;

	return 0;
}

int zx_flash_find_partition(const char *name, u32 *offset, u32 *size)
{
	u32 count = zx_flash_get_part_count();
	char pname[32];
	u32 poff, psize;
	u32 i;

	if (!name)
		return -EINVAL;

	for (i = 0; i < count; i++) {
		if (zx_flash_get_partition_info(i, pname, &poff, &psize) == 0) {
			if (!strcasecmp(pname, name)) {
				if (offset)
					*offset = poff;
				if (size)
					*size = psize;
				return 0;
			}
		}
	}

	return -ENOENT;
}

int zx_flash_generate_mtdparts(char *buf, size_t maxlen)
{
	int count = zx_flash_get_part_count();
	char pname[32];
	u32 poff, psize;
	char *p = buf;
	int first = 1;
	int i;

	if (!buf || maxlen < 64)
		return -EINVAL;

	if (!g_flash.initialized)
		zx_flash_init();

	if (g_flash.type == FLASH_TYPE_NAND)
		p += snprintf(p, maxlen - (p - buf), "mtdparts=spi-nand:");
	else
		p += snprintf(p, maxlen - (p - buf), "mtdparts=spi-nor-dt:");

	for (i = 0; i < count; i++) {
		if (zx_flash_get_partition_info(i, pname, &poff, &psize) != 0)
			continue;
		/* Skip raw / ddr / full / virtual partitions */
		if (!strcmp(pname, "ddr") || !strcmp(pname, "raw") || !strcmp(pname, "full") || psize == 0xFFFFFFFF)
			continue;

		if (!first)
			p += snprintf(p, maxlen - (p - buf), ",");
		first = 0;

		if ((psize >= 0x100000) && (psize % 0x100000 == 0))
			p += snprintf(p, maxlen - (p - buf), "%um@0x%x(%s)", psize >> 20, poff, pname);
		else
			p += snprintf(p, maxlen - (p - buf), "%uk@0x%x(%s)", psize >> 10, poff, pname);
	}

	return 0;
}

/*
 * Map a logical firmware type to its physical flash window.  Shared by the
 * whole-image writer and the slice writer so both resolve partitions the same
 * way.
 */
static int zx_flash_resolve_target(failsafe_fw_t fw, const char *custom_part,
				   u32 *target_offset, u32 *max_allowed,
				   const char **part_name)
{
	switch (fw) {
	case FW_TYPE_ROOTFS:
		*part_name = "rootfs";
		if (zx_flash_find_partition("rootfs", target_offset, max_allowed) != 0) {
			*target_offset = 0x00468000;
			*max_allowed = g_flash.total_size - *target_offset;
		}
		break;
	case FW_TYPE_IMAGEFS:
		*part_name = "imagefs";
		if (zx_flash_find_partition("imagefs", target_offset, max_allowed) != 0 &&
		    zx_flash_find_partition("firmware", target_offset, max_allowed) != 0) {
			*target_offset = 0x00068000;
			*max_allowed = g_flash.total_size - *target_offset;
		}
		break;
	case FW_TYPE_UBOOT:
		*part_name = "uboot";
		if (zx_flash_find_partition("uboot", target_offset, max_allowed) != 0) {
			*target_offset = 0x00040000;
			*max_allowed = 0x00030000;
		}
		break;
	case FW_TYPE_NVROFS:
		*part_name = "nvrofs";
		if (zx_flash_find_partition("nvrofs", target_offset, max_allowed) != 0) {
			*target_offset = 0x00008000;
			*max_allowed = 0x00038000;
		}
		break;
	case FW_TYPE_USERDATA:
		*part_name = "userdata";
		if (zx_flash_find_partition("userdata", target_offset, max_allowed) != 0) {
			*target_offset = 0x00780000;
			*max_allowed = 0x00080000;
		}
		break;
	case FW_TYPE_ZLOADER:
		*part_name = "zloader";
		if (zx_flash_find_partition("zloader", target_offset, max_allowed) != 0) {
			*target_offset = 0x00000000;
			*max_allowed = 0x00008000;
		}
		break;
	case FW_TYPE_FULL:
		*part_name = "full";
		*target_offset = 0x00000000;
		*max_allowed = g_flash.total_size;
		break;
	case FW_TYPE_CUSTOM:
		if (custom_part && custom_part[0] == '0' && (custom_part[1] == 'x' || custom_part[1] == 'X')) {
			*target_offset = simple_strtoul(custom_part, NULL, 16);
			*max_allowed = g_flash.total_size - *target_offset;
		} else if (custom_part && zx_flash_find_partition(custom_part, target_offset, max_allowed) == 0) {
			/* Found in dynamic or static partition table */
		} else {
			*target_offset = 0x00468000;
			*max_allowed = g_flash.total_size - *target_offset;
		}
		*part_name = custom_part ? custom_part : "custom";
		break;
	default:
		printf("Invalid firmware type: %d\n", fw);
		return -EINVAL;
	}

	return 0;
}

/*
 * Erase and/or program one slice of an already-resolved image.
 *
 * The web UI drives the "upload then confirm" path slice by slice through
 * /flash_step so it can show a live progress bar.  op selects which stage this
 * call runs: an erase-only pass walks the whole image first, then a write-only
 * pass walks it again (ZX_FLASH_OP_BOTH restores the old single-call behaviour).
 * The image must already be validated against the region (offset + len within
 * total, and total within the partition limit).
 */
int zx_flash_write_slice(const void *data, size_t total, failsafe_fw_t fw,
			 const char *custom_part, u32 offset, u32 len, int op,
			 u32 *erase_ms, u32 *write_ms)
{
	u32 target_offset = 0;
	u32 max_allowed = 0;
	const char *part_name = "unknown";
	ulong t;
	int ret;

	if (!g_flash.initialized)
		zx_flash_init();

	if (g_flash.type == FLASH_TYPE_UNKNOWN || g_flash.total_size == 0) {
		printf("[ZX_FLASH]: Cannot write: Flash is not detected or size is 0!\n");
		return -ENODEV;
	}

	if ((op & ZX_FLASH_OP_BOTH) == 0)
		return -EINVAL;

	ret = zx_flash_resolve_target(fw, custom_part, &target_offset, &max_allowed,
				      &part_name);
	if (ret)
		return ret;

	if (!len || total > max_allowed || offset >= total || len > total - offset)
		return -EINVAL;

	/* An erase-only pass never dereferences data, so callers may pass NULL */
	if ((op & ZX_FLASH_OP_WRITE) && !data)
		return -EINVAL;

	u32 phys_addr = zx_flash_logical_to_physical(target_offset, offset);

	printf("[ZX_FLASH]: '%s' slice +0x%X len 0x%X (of %zu) -> flash 0x%08X op=%s%s\n",
	       part_name, offset, len, total, phys_addr,
	       (op & ZX_FLASH_OP_ERASE) ? "erase" : "",
	       (op & ZX_FLASH_OP_WRITE) ? ((op & ZX_FLASH_OP_ERASE) ? "+write" : "write") : "");

	/* Split erase and program timing so the progress page can show where the
	 * wall-clock time actually goes (the two have very different limits). */
	if (op & ZX_FLASH_OP_ERASE) {
		t = get_timer(0);
		ret = zx_flash_erase(phys_addr, len);
		if (erase_ms)
			*erase_ms = get_timer(t);
		if (ret)
			return ret;
	}

	if (op & ZX_FLASH_OP_WRITE) {
		t = get_timer(0);
		ret = zx_flash_write(phys_addr, (const u8 *)data + offset, len);
		if (write_ms)
			*write_ms = get_timer(t);
		if (ret)
			return ret;

		/* Also update uboot-mirr backup when the last slice of U-Boot completes */
		if (fw == FW_TYPE_UBOOT && offset + len >= total) {
			u32 mirr_offset = 0, mirr_size = 0;

			if (zx_flash_find_partition("uboot-mirr", &mirr_offset, &mirr_size) == 0 &&
			    total <= mirr_size) {
				printf("[ZX_FLASH]: Updating uboot-mirr backup at 0x%08X...\n", mirr_offset);
				zx_flash_erase(mirr_offset, total);
				zx_flash_write(mirr_offset, data, total);
			}
		}
	}

	return 0;
}

int zx_flash_write_image(const void *data, size_t size, failsafe_fw_t fw, const char *custom_part)
{
	u32 target_offset = 0;
	u32 max_allowed = 0;
	const char *part_name = "unknown";
	int ret = 0;

	if (!g_flash.initialized)
		zx_flash_init();

	if (g_flash.type == FLASH_TYPE_UNKNOWN || g_flash.total_size == 0) {
		printf("[ZX_FLASH]: Cannot write: Flash is not detected or size is 0!\n");
		return -ENODEV;
	}

	ret = zx_flash_resolve_target(fw, custom_part, &target_offset, &max_allowed,
				      &part_name);
	if (ret)
		return ret;

	if (size > max_allowed) {
		printf("Error: File size (0x%zx) exceeds partition max size (0x%x)!\n",
		       size, max_allowed);
		return -EFBIG;
	}

	printf("\n*** Starting ZX297520 Flash Upgrade ***\n");
	printf("Target Partition: %s\n", part_name);
	printf("Flash Address:    0x%08X\n", target_offset);
	printf("Image Size:       %zu bytes (0x%zx)\n", size, size);
	printf("Flash Type:       %s\n", g_flash.model);

	printf("\n[1/3] Erasing Flash blocks...\n");
	ret = zx_flash_erase(target_offset, size);
	if (ret) {
		printf("[-] Erase failed with code %d\n", ret);
		return ret;
	}

	printf("[2/3] Writing image data to Flash...\n");
	ret = zx_flash_write(target_offset, data, size);
	if (ret) {
		printf("[-] Write failed with code %d\n", ret);
		return ret;
	}

	/* Also write mirror backup for U-Boot if uboot-mirr partition exists */
	if (fw == FW_TYPE_UBOOT) {
		u32 mirr_offset = 0, mirr_size = 0;
		if (zx_flash_find_partition("uboot-mirr", &mirr_offset, &mirr_size) == 0 && size <= mirr_size) {
			printf("[*] Updating uboot-mirr backup at 0x%08X...\n", mirr_offset);
			zx_flash_erase(mirr_offset, size);
			zx_flash_write(mirr_offset, data, size);
		}
	}

	printf("[3/3] Upgrade complete! Verifying integrity...\n");
	printf("[+] Flash upgrade successful for %s!\n\n", part_name);

	return 0;
}
