// SPDX-License-Identifier: GPL-2.0-only

#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include "clk-regmap-clkdet.h"

struct clk_regmap_clkdet_desc {
	u32 ctrl_rstn_bit;
	u32 ctrl_cnten_bit;
	u32 stat_ofs;
	u32 stat_done_bit;
	u32 cnt_mask;
	u32 cnt_shift;
	u32 no_polling_done;
};

static const struct clk_regmap_clkdet_desc clk_det_descs[3] = {
	[CLK_DET_TYPE_GENERIC] = {
		.ctrl_rstn_bit = 0,
		.ctrl_cnten_bit = 1,
		.stat_ofs = 0x0,
		.stat_done_bit = 30,
		.cnt_mask = GENMASK(29, 13),
		.cnt_shift = 13,
		.no_polling_done = 0,
	},
	[CLK_DET_TYPE_SC_WRAP] = {
		.ctrl_rstn_bit = 17,
		.ctrl_cnten_bit = 16,
		.stat_ofs = 0x8,
		.stat_done_bit = 0,
		.cnt_mask = GENMASK(17, 1),
		.cnt_shift = 1,
	},
	[CLK_DET_TYPE_HDMI_TOP] = {
		.ctrl_rstn_bit = 0,
		.ctrl_cnten_bit = 1,
		.stat_ofs = 0x0,
		.cnt_mask = GENMASK(29, 13),
		.cnt_shift = 13,
		.no_polling_done = 1,
	}
};

static DEFINE_MUTEX(clk_regmap_clkdet_lock);

static unsigned long clk_regmap_clkdet_eval_freq(struct clk_regmap_clkdet *clkd)
{
	const struct clk_regmap_clkdet_desc *desc = &clk_det_descs[clkd->type];
	u32 ctrl_mask;
	u32 val;
	unsigned long freq = 0;
	int ret = 0;

	mutex_lock(&clk_regmap_clkdet_lock);

	ctrl_mask = BIT(desc->ctrl_rstn_bit) | BIT(desc->ctrl_cnten_bit);
	clk_regmap_update_bits(&clkd->clkr, clkd->ofs, ctrl_mask, 0);
	clk_regmap_update_bits(&clkd->clkr, clkd->ofs, ctrl_mask, BIT(desc->ctrl_rstn_bit));
	clk_regmap_update_bits(&clkd->clkr, clkd->ofs, ctrl_mask, ctrl_mask);

	if (desc->no_polling_done)
		msleep(10);
	else
		ret = regmap_read_poll_timeout(clkd->clkr.regmap, clkd->ofs + desc->stat_ofs, val,
			val & BIT(desc->stat_done_bit), 0, 100);
	if (!ret) {
		val = clk_regmap_read(&clkd->clkr, clkd->ofs + desc->stat_ofs);
		freq = ((val & desc->cnt_mask) >> desc->cnt_shift) * 100000;
	}

	clk_regmap_update_bits(&clkd->clkr, clkd->ofs, ctrl_mask, 0);

	mutex_unlock(&clk_regmap_clkdet_lock);

	return freq;
}

static unsigned long clk_regmap_clkdet_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct clk_regmap_clkdet *clkd = to_clk_regmap_clkdet(hw);

	return clk_regmap_clkdet_eval_freq(clkd);
}

const struct clk_ops clk_regmap_clkdet_ops = {
	.recalc_rate = clk_regmap_clkdet_recalc_rate,
};
EXPORT_SYMBOL_GPL(clk_regmap_clkdet_ops);
