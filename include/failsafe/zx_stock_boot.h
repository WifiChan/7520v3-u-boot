/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026 Wifi Chan Lab
 * Platform: Sanechip ZX297520V3 (Cortex-A53, 32-bit AArch32)
 * Stock Factory Firmware Loader Header
 */

#ifndef _ZX_STOCK_BOOT_H_
#define _ZX_STOCK_BOOT_H_

#include <linux/types.h>

/* Attempt to load and boot factory stock Linux firmware from 'imagefs' */
int zx_boot_stock_linux(void);

/*
 * 标准路径公共内核命令行：不含 root=，与原厂一致（由 mtdparts 让内核自行挂载根文件系统）。
 * 追加 " %s" 形式的 mtdparts 即为完整 bootargs。
 */
#define ZX_STD_BOOTARGS_BASE \
	"console=ttyS1,115200 earlyprintk no_console_suspend loglevel=8 " \
	"lpj=3120000 initcall_debug clk_ignore_unused EnhancedSecurity=0 " \
	"boot_reason=0 system=normal pmu_type=1"

#endif /* _ZX_STOCK_BOOT_H_ */
