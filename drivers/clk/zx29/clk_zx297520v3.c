// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2026 Wifi Chan Lab
 * Sanechip ZX297520V3 Clock Driver
 */

#include "linux/kernel.h"
#include <dm.h>
#include <clk-uclass.h>
#include <i2c.h>
#include <dm/device_compat.h>
#include <linux/bitops.h>
#include <asm/io.h>
#include <dt-bindings/clk/zx297520v3.h>

enum zx29_type {
	ZX29_CLK_FIXED,
	ZX29_CLK_MUX,
	ZX29_CLK_GATE,
};

struct zx29_clk_desc {
	const u32 id;
	const enum zx29_type type;
	const u8 offset;
	const u8 shift;
	const u8 width;
	const u8 num_parents;
	const u32 *parents;
	const u32 rate;
};

struct zx297520v3_clk_data {
  void __iomem *base;
  const struct zx297520v3_clk_of_match_data *clk_data;
};

struct zx297520v3_clk_of_match_data {
	const struct zx29_clk_desc *fixed_clks;
	u32 num_fixed_clks;
	const struct zx29_clk_desc *top_clks;
	u32 num_top_clks;
	const struct zx29_clk_desc *matrix_clks;
	u32 num_matrix_clks;
};

#define FIXED(_id, _rate) \
	{ .id = _id, .rate = _rate }

#define GATE(_id, _offset, _shift) \
	{ .id = _id, .type = ZX29_CLK_GATE, .offset = _offset, .shift = _shift }

#define MUX(_id, _offset, _shift, _width, _parents) \
	{ \
		.id = _id, \
		.type = ZX29_CLK_MUX, \
		.offset = _offset, \
		.shift = _shift, \
		.width = _width, \
		.parents = _parents, \
		.num_parents = ARRAY_SIZE(_parents) \
	}

static const struct zx29_clk_desc fixed_clks[] = {
	FIXED(ZX297520_CLK25M,	25000000),
	FIXED(ZX297520_CLK26M,	26000000),
	FIXED(ZX297520_CLK39M,	39000000),
	FIXED(ZX297520_CLK50M,	50000000),
	FIXED(ZX297520_CLK78M,	78000000),
	FIXED(ZX297520_CLK100M,	100000000),
	FIXED(ZX297520_CLK104M,	104000000),
	FIXED(ZX297520_CLK156M,	156000000),
	FIXED(ZX297520_CLK312M,	312000000),
	FIXED(ZX297520_CLK624M,	624000000),
};

static const u32 ahb_parents[] = {
	ZX297520_CLK26M,
	ZX297520_CLK104M,
	ZX297520_CLK78M,
};

static const u32 uart_wclk_parents[] = {
	ZX297520_CLK26M,
	ZX297520_CLK104M,
};

static const struct zx29_clk_desc top_clks[] = {
	MUX(ZX297520_AHB_MUX, 0x3c, 4, 2, ahb_parents),
	MUX(ZX297520_UART0_WCLK_MUX, 0x40, 2, 1, uart_wclk_parents),
	GATE(ZX297520_USB_24M, 0x6c, 3),
	GATE(ZX297520_USB_AHB, 0x6c, 4),
	GATE(ZX297520_UART0_WCLK, 0x5c, 12),
	GATE(ZX297520_UART0_PCLK, 0x5c, 13),
};

static const u32 cpu_wclk_parents[] = {
	ZX297520_CLK26M,
	ZX297520_CLK624M,
	ZX297520_CLK312M,
	ZX297520_CLK156M,
};

static const u32 sd1_wclk_parents[] = {
	ZX297520_CLK26M,
	ZX297520_CLK100M,
	ZX297520_CLK78M,
	ZX297520_CLK50M,
	ZX297520_CLK39M,
	ZX297520_CLK25M,
};

static const struct zx29_clk_desc matrix_clks_v1[] = {
	MUX(ZX297520_CPU_WCLK, 0x20, 0, 2, cpu_wclk_parents),
	MUX(ZX297520_SD1_WCLK_MUX, 0x50, 8, 3, sd1_wclk_parents),
	GATE(ZX297520_SD1_PCLK, 0x50, 4),
	GATE(ZX297520_SD1_WCLK, 0x50, 5),
};

static const struct zx29_clk_desc matrix_clks_v2[] = {
	MUX(ZX297520_CPU_WCLK, 0x40, 0, 2, cpu_wclk_parents),
	MUX(ZX297520_SD1_WCLK_MUX, 0x50, 8, 3, sd1_wclk_parents),
};

static const struct zx29_clk_desc *zx29_get_clk(struct zx297520v3_clk_data *data, ulong id)
{
  const struct zx297520v3_clk_of_match_data *clk_data = data->clk_data;
  int i;

	for (i = 0; i < clk_data->num_fixed_clks; i++)
		if (clk_data->fixed_clks[i].id == id)
			return &clk_data->fixed_clks[i];

	for (i = 0; i < clk_data->num_top_clks; i++)
		if (clk_data->top_clks[i].id == id)
			return &clk_data->top_clks[i];

	for (i = 0; i < clk_data->num_matrix_clks; i++)
		if (clk_data->matrix_clks[i].id == id)
			return &clk_data->matrix_clks[i];

	return NULL;
}

static int zx_clk_enable(struct clk *clk)
{
	struct zx297520v3_clk_data *data = dev_get_priv(clk->dev);
	const struct zx29_clk_desc *desc;
	u32 val;

	desc = zx29_get_clk(data, clk->id);
	if (!desc)
		return -EINVAL;

	if (desc->type != ZX29_CLK_GATE)
		return 0;

	val = readl(data->base + desc->offset);
	val |= BIT(desc->shift);
	writel(val, data->base + desc->offset);

	return 0;
}

static int zx_clk_disable(struct clk *clk)
{
	struct zx297520v3_clk_data *data = dev_get_priv(clk->dev);
	const struct zx29_clk_desc *desc;
	u32 val;

	desc = zx29_get_clk(data, clk->id);
	if (!desc)
		return -EINVAL;

	if (desc->type != ZX29_CLK_GATE)
		return 0;

	val = readl(data->base + desc->offset);
	val &= ~BIT(desc->shift);
	writel(val, data->base + desc->offset);

	return 0;
}

static int zx_clk_set_parent(struct clk *clk, struct clk *parent)
{
	struct zx297520v3_clk_data *data = dev_get_priv(clk->dev);
	const struct zx29_clk_desc *desc;
	u32 i, val, mask;

	desc = zx29_get_clk(data, clk->id);
	if (!desc)
		return -EINVAL;
	else if (desc->type != ZX29_CLK_MUX)
		return -EINVAL;

	for (i = 0; i < desc->num_parents; i++) {
		if (desc->parents[i] == parent->id)
			break;
	}

	if (i == desc->num_parents)
		return -EINVAL;

	mask = GENMASK(desc->shift + desc->width - 1, desc->shift);

	val = readl(data->base + desc->offset);
	val &= ~mask;
	val |= (i << desc->shift);

	writel(val, data->base + desc->offset);

	return 0;
}

static ulong zx_clk_get_rate(struct clk *clk)
{
	struct zx297520v3_clk_data *data = dev_get_priv(clk->dev);
	const struct zx29_clk_desc *desc;
	struct clk inner;
	u32 reg;
	int idx;

	desc = zx29_get_clk(data, clk->id);
	if (!desc)
		return 0;

	switch (desc->type) {
	case ZX29_CLK_FIXED:
		return desc->rate;
	case ZX29_CLK_MUX:
		reg = readl(data->base + desc->offset);
		idx = (reg >> desc->shift) & (BIT(desc->width) - 1);

		if (idx >= desc->num_parents)
			return 0;

		inner.dev = clk->dev;
		inner.id = desc->parents[idx];
		return zx_clk_get_rate(&inner);

	default:
		return 0;
	}
}

static const struct clk_ops zx297520v3_clk_ops = {
	.enable = zx_clk_enable,
	.disable = zx_clk_disable,
	.set_parent = zx_clk_set_parent,
	.get_rate = zx_clk_get_rate,
};

static int zx297520v3_clk_probe(struct udevice *dev)
{
	struct zx297520v3_clk_data *data = dev_get_priv(dev);
	const struct zx297520v3_clk_of_match_data *match_data = (struct zx297520v3_clk_of_match_data *)dev_get_driver_data(dev);

	data->base = dev_read_addr_ptr(dev);
	if (!data->base)
		return -EINVAL;

	data->clk_data = match_data;

	return 0;
}

static const struct zx297520v3_clk_of_match_data top_match_data = {
	.fixed_clks = fixed_clks,
	.num_fixed_clks = ARRAY_SIZE(fixed_clks),
	.top_clks = top_clks,
	.num_top_clks = ARRAY_SIZE(top_clks),
};

static const struct zx297520v3_clk_of_match_data matrix_v1_match_data = {
	.fixed_clks = fixed_clks,
	.num_fixed_clks = ARRAY_SIZE(fixed_clks),
	.matrix_clks = matrix_clks_v1,
	.num_matrix_clks = ARRAY_SIZE(matrix_clks_v1)
};

static const struct zx297520v3_clk_of_match_data matrix_v2_match_data = {
	.fixed_clks = fixed_clks,
	.num_fixed_clks = ARRAY_SIZE(fixed_clks),
	.matrix_clks = matrix_clks_v2,
	.num_matrix_clks = ARRAY_SIZE(matrix_clks_v2)
};

static const struct udevice_id zx297520v3_top_of_match[] = {
	{ .compatible = "sanechips,zx297520v3-topcrm-clk", .data = (ulong)&top_match_data },
	{ /* sentinel */ },
};

static const struct udevice_id zx297520v3_matrix_of_match[] = {
	{ .compatible = "sanechips,zx297520v3-matrix-clk", .data = (ulong)&matrix_v1_match_data },
	{ .compatible = "sanechips,zx297520v3-matrix-clk-v2", .data = (ulong)&matrix_v2_match_data },
	{ /* sentinel */ },
};

U_BOOT_DRIVER(zx297520v3_topcrm_clk) = {
	.name = "zx297520v3-topcrm-clk",
	.id = UCLASS_CLK,
	.of_match = zx297520v3_top_of_match,
	.probe = zx297520v3_clk_probe,
	.priv_auto	= sizeof(struct zx297520v3_clk_data),
	.ops = &zx297520v3_clk_ops,
};

U_BOOT_DRIVER(zx297520v3_matrix_clk) = {
	.name = "zx297520v3-matrix-clk",
	.id = UCLASS_CLK,
	.of_match = zx297520v3_matrix_of_match,
	.probe = zx297520v3_clk_probe,
	.priv_auto	= sizeof(struct zx297520v3_clk_data),
	.ops = &zx297520v3_clk_ops,
};
