// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Wifi Chan Lab
 * Platform: Sanechip ZX297520V3 (Cortex-A53, 32-bit AArch32)
 * SoC CPU & PLL Early Initialization
 */

#include <cpu_func.h>
#include <dm.h>
#include <init.h>
#include <dm/uclass-internal.h>
#include <asm/io.h>

#define TOP_SYSCLK_BASE				0x0013b000
#define PLL_CPU_BUS_CFG0_REG	TOP_SYSCLK_BASE + 0x018
#define PLL_CPU_BUS_CFG1_REG	TOP_SYSCLK_BASE + 0x01C

#define PLL_CFG0_LOCK					BIT(30)

static bool pll_unlocked(void) {
	return (readl(PLL_CPU_BUS_CFG0_REG) & PLL_CFG0_LOCK) == 0;
}

int arch_cpu_init(void)
{
	icache_enable();

	if (pll_unlocked()) {
		writel(BIT(24), PLL_CPU_BUS_CFG1_REG);
		writel(0x08348989, PLL_CPU_BUS_CFG0_REG);
		while (pll_unlocked());
	}

	return 0;
}

void enable_caches(void)
{
	/* Enable D-cache. I-cache is already enabled in start.S */
	dcache_enable();
}

