/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026 Wifi Chan Lab
 * Platform: Sanechip ZX297520V3 (Cortex-A53, 32-bit AArch32)
 * Unified SPI NOR / SPI NAND Flash Controller Interface
 */

#ifndef _ZX_FLASH_H_
#define _ZX_FLASH_H_

#include <linux/types.h>
#include <failsafe/fw_type.h>

#define FLASH_TYPE_UNKNOWN  0
#define FLASH_TYPE_NOR      1
#define FLASH_TYPE_NAND     2

struct zx_flash_desc {
	int type;             /* FLASH_TYPE_NOR or FLASH_TYPE_NAND */
	u8 manuf_id;
	u16 chip_id;
	char model[32];
	u32 total_size;       /* Flash capacity in bytes */
	u32 page_size;        /* Page program size (256B for NOR, 2048B for NAND) */
	u32 erase_size;       /* Erase block size (64KB for NOR, 128KB for NAND) */
	u32 block_count;
	int initialized;
};

/* Flash subsystem initialization & probing */
int zx_flash_init(void);

/* Flash query APIs */
int zx_flash_get_type(void);
u32 zx_flash_get_size(void);
const char *zx_flash_get_name(void);
int zx_flash_get_info_str(char *buf, size_t maxlen);
int zx_flash_get_part_count(void);
int zx_flash_get_partition_info(u32 idx, char *name, u32 *offset, u32 *size);
int zx_flash_find_partition(const char *name, u32 *offset, u32 *size);
int zx_flash_has_partition_table(void);
int zx_flash_generate_mtdparts(char *buf, size_t maxlen);

/* Low-level Flash R/W/Erase APIs */
int zx_flash_read(u32 addr, void *buf, size_t len);
int zx_flash_erase(u32 addr, size_t len);
int zx_flash_write(u32 addr, const void *buf, size_t len);

/* High-level Failsafe Firmware Flashing API */
int zx_flash_write_image(const void *data, size_t size, failsafe_fw_t fw, const char *custom_part);

#endif /* _ZX_FLASH_H_ */
