// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Wifi Chan Lab
 * Platform: Sanechip ZX297520V3 (Cortex-A53, 32-bit AArch32)
 * SoC DRAM & Reset Initialization
 */

#include <cpu_func.h>
#include <fdtdec.h>
#include <init.h>
#include <asm/system.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/sizes.h>

DECLARE_GLOBAL_DATA_PTR;

int print_cpuinfo(void)
{
	printf("CPU:   Sanechip ZX297520V3\n");
	return 0;
}

int dram_init(void)
{
	int ret;

	ret = fdtdec_setup_mem_size_base();
	if (ret)
		return ret;

	gd->ram_size = get_ram_size((void *)gd->ram_base, SZ_128M);

	return 0;
}

#define TOP_SYSCLK_BASE		0x0013b000
#define WDT_RESET_TYPE		(TOP_SYSCLK_BASE + 0x2c)
#define RM_MOD_RST		(TOP_SYSCLK_BASE + 0x74)
#define RM_WDT_BASE		0x00148000
#define RM_WDT_START_REG	(RM_WDT_BASE + 0x1c)
#define ZX_USB_BASE_ADDR	0x01500000

void reset_cpu(void)
{
	printf("\n======================================================\n");
	printf("  [RESET] Device stopped. Please replug USB / power cycle.\n");
	printf("======================================================\n\n");

	/*
	 * Quiesce USB DWC2 DMA/interrupts and assert Soft Disconnect
	 * so the host PC immediately sees USB disconnect.
	 */
	writel(readl(ZX_USB_BASE_ADDR + 0x008) & ~1U, ZX_USB_BASE_ADDR + 0x008);
	writel(0, ZX_USB_BASE_ADDR + 0x018);
	writel(readl(ZX_USB_BASE_ADDR + 0x804) | (1U << 1), ZX_USB_BASE_ADDR + 0x804);

	/* Trigger hardware reset registers */
	writel(readl(WDT_RESET_TYPE) | 0x3 | (0x3 << 6), WDT_RESET_TYPE);
	writel(0x12340001, RM_WDT_START_REG);
	writel(1, TOP_SYSCLK_BASE);

	while (1) {
		writel(1, TOP_SYSCLK_BASE);
		asm volatile("wfi");
	}
}

