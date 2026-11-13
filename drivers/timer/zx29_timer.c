// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Wifi Chan Lab
 * Sanechip ZX297520V3 Timer Driver
 */

#include <clk.h>
#include <dm.h>
#include <timer.h>
#include <asm/io.h>
#include <linux/bitops.h>

#define TIMER_LOAD_VAL	0xffffffff       

#define PTV_DIV_1				0
#define AUTO						BIT(1)
#define START						BIT(0)

#define CONFIG_REG			0x04
#define LOAD_REG				0x08
#define START_REG				0x0c
#define SET_EN_REG 			0x10
#define COUNT_REG				0x18

struct zx29_timer_priv {
	void __iomem *base;
};

static u64 zx29_timer_get_count(struct udevice *dev)
{
	struct zx29_timer_priv *priv = dev_get_priv(dev);
	u32 val = readl(priv->base + COUNT_REG);

	return timer_conv_64(TIMER_LOAD_VAL - val);
}

static int zx29_timer_probe(struct udevice *dev)
{
	struct timer_dev_priv *uc_priv = dev_get_uclass_priv(dev);
	struct zx29_timer_priv *priv = dev_get_priv(dev);
	struct clk clk;
	int ret;

	priv->base = dev_read_addr_ptr(dev);

	if (!priv->base)
		return -ENOENT;

	ret = clk_get_by_index(dev, 0, &clk);
	if (ret)
		return ret;

	uc_priv->clock_rate = clk_get_rate(&clk);
	if (!uc_priv->clock_rate)
		return -EINVAL;

	writel_relaxed(0, priv->base + START_REG);
	writel_relaxed(PTV_DIV_1 | AUTO, priv->base + CONFIG_REG);
	writel_relaxed(TIMER_LOAD_VAL, priv->base + LOAD_REG);
	writel_relaxed(readl_relaxed(priv->base + SET_EN_REG) ^ 0xf, priv->base + SET_EN_REG);
	writel_relaxed(START, priv->base + START_REG);

	return 0;
}

static const struct timer_ops zx29_timer_ops = {
	.get_count = zx29_timer_get_count,
};

static const struct udevice_id zx29_timer_ids[] = {
	{ .compatible = "sanechips,timer", },
	{ }
};

U_BOOT_DRIVER(zx29_timer) = {
	.name = "zx29_timer",
	.id = UCLASS_TIMER,
	.of_match = zx29_timer_ids,
	.priv_auto	= sizeof(struct zx29_timer_priv),
	.probe = zx29_timer_probe,
	.ops = &zx29_timer_ops,
	.flags = DM_FLAG_PRE_RELOC,
};

