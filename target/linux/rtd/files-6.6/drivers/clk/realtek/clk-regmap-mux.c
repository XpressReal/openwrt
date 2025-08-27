// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/module.h>
#include "clk-regmap-mux.h"

static u8 clk_regmap_mux_get_parent(struct clk_hw *hw)
{
	struct clk_regmap_mux *clkm = to_clk_regmap_mux(hw);
	int num_parents = clk_hw_get_num_parents(hw);
	u32 val;

	val = clk_regmap_read(&clkm->clkr, clkm->mux_ofs) >> clkm->shift;

	val &= clkm->mask;
	if (val >= num_parents)
		return -EINVAL;

	return val;
}

static int clk_regmap_mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_regmap_mux *clkm = to_clk_regmap_mux(hw);

	pr_debug("%pC: clk set parent, idx=%d\n", hw->clk, index);

	clk_regmap_update_bits(&clkm->clkr, clkm->mux_ofs, clkm->mask << clkm->shift,
		index << clkm->shift);

	return 0;
}

const struct clk_ops clk_regmap_mux_ops = {
	.get_parent = clk_regmap_mux_get_parent,
	.set_parent = clk_regmap_mux_set_parent,
	.determine_rate = __clk_mux_determine_rate,
};
EXPORT_SYMBOL_GPL(clk_regmap_mux_ops);

const struct clk_ops clk_regmap_mux_ro_ops = {
	.get_parent = clk_regmap_mux_get_parent,
};
EXPORT_SYMBOL_GPL(clk_regmap_mux_ro_ops);

