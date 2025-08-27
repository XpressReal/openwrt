// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021 Realtek Semiconductor Corp.
 */

#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/printk.h>
#include <linux/regmap.h>
#include <linux/spinlock.h>
#include "clk-dco.h"

static DEFINE_SPINLOCK(dco_lock);

#define ISO_PLL_ETN_OSC  0x7A4
#define ISO_PLL_ETN_OSC_REG_TC_EMB  GENMASK(11, 9)
#define ISO_PLL_ETN_OSC_S_EMB       GENMASK(7, 1)
#define ISO_PLL_ETN_OSC_RSTB_EMB    BIT(0)

static void pll_etn_osc_set_osc_rstb(struct clk_regmap *clkr)
{
	clk_regmap_update_bits(clkr, ISO_PLL_ETN_OSC, ISO_PLL_ETN_OSC_RSTB_EMB,
			       FIELD_PREP(ISO_PLL_ETN_OSC_RSTB_EMB, 0));
	clk_regmap_update_bits(clkr, ISO_PLL_ETN_OSC, ISO_PLL_ETN_OSC_RSTB_EMB,
			       FIELD_PREP(ISO_PLL_ETN_OSC_RSTB_EMB, 1));
}

static u32 pll_etn_osc_get_osc_sig(struct clk_regmap *clkr)
{
	u32 val;

	val = clk_regmap_read(clkr, ISO_PLL_ETN_OSC);
	return FIELD_GET(ISO_PLL_ETN_OSC_S_EMB, val);
}

static void pll_etn_osc_set_osc_sig(struct clk_regmap *clkr, u32 s_emb)
{
	u32 v, m;

	m = ISO_PLL_ETN_OSC_REG_TC_EMB | ISO_PLL_ETN_OSC_S_EMB;
	v = FIELD_PREP(ISO_PLL_ETN_OSC_REG_TC_EMB, 4) |
	    FIELD_PREP(ISO_PLL_ETN_OSC_S_EMB, s_emb);

	clk_regmap_update_bits(clkr, ISO_PLL_ETN_OSC, m, v);
}

#define ISO_DCO_0_OSC_COUNT_LIMIT   GENMASK(21, 10)
#define ISO_DCO_0_XTAL_OFF          BIT(2)
#define ISO_DCO_0_OSC_SEL           BIT(1)
#define ISO_DCO_0_CAL_ENABLE        BIT(0)

#define ISO_DCO_1_DCO_COUNT_LATCH   GENMASK(24, 13)
#define ISO_DCO_1_XTAL_COUNT_LATCH  GENMASK(12, 1)
#define ISO_DCO_1_DCO_CAL_DONE      BIT(0)


struct dco_reg_desc {
	u32 reg_dco_0;
	u32 reg_dco_1;
	u32 reg_dco_2;
	u32 dco_sel_in_dco_2 : 1;
};

static const struct dco_reg_desc dco_reg_desc[] = {
	{
		.reg_dco_0 = 0x0f4,
		.reg_dco_1 = 0x79c,
		.dco_sel_in_dco_2 = 0,
	},
	{
		.reg_dco_0 = 0x0, /* 0x3f0 */
		.reg_dco_1 = 0x4,
		.reg_dco_2 = 0x8,
		.dco_sel_in_dco_2 = 1,
	},
};

static void dco_set_cal_en(struct clk_dco_data *data, u32 val)
{
	const struct dco_reg_desc *desc = &dco_reg_desc[data->loc];

	clk_regmap_update_bits(&data->clkr, desc->reg_dco_0, ISO_DCO_0_CAL_ENABLE,
			   FIELD_PREP(ISO_DCO_0_CAL_ENABLE, val));
}

static void dco_set_dco_sel(struct clk_dco_data *data, u32 val)
{
	const struct dco_reg_desc *desc = &dco_reg_desc[data->loc];
	u32 reg_dco_sel = desc->dco_sel_in_dco_2 ? desc->reg_dco_2 : desc->reg_dco_0;

	clk_regmap_update_bits(&data->clkr, reg_dco_sel, ISO_DCO_0_OSC_SEL,
			   FIELD_PREP(ISO_DCO_0_OSC_SEL, val));
}

static void dco_set_xtal_off(struct clk_dco_data *data, u32 val)
{
	const struct dco_reg_desc *desc = &dco_reg_desc[data->loc];

	clk_regmap_update_bits(&data->clkr, desc->reg_dco_0, ISO_DCO_0_XTAL_OFF,
			       FIELD_PREP(ISO_DCO_0_XTAL_OFF, val));
}

static void dco_set_osc_count_limit(struct clk_dco_data *data, u32 val)
{
	const struct dco_reg_desc *desc = &dco_reg_desc[data->loc];

	clk_regmap_update_bits(&data->clkr, desc->reg_dco_0, ISO_DCO_0_OSC_COUNT_LIMIT,
			       FIELD_PREP(ISO_DCO_0_OSC_COUNT_LIMIT, val));
}

static void dco_clear_cal_data(struct clk_dco_data *data)
{
	const struct dco_reg_desc *desc = &dco_reg_desc[data->loc];

	clk_regmap_write(&data->clkr, desc->reg_dco_1, 0);
}

static int dco_wait_cal_done(struct clk_dco_data *data)
{
	const struct dco_reg_desc *desc = &dco_reg_desc[data->loc];
	u32 val;

	return regmap_read_poll_timeout_atomic(data->clkr.regmap, desc->reg_dco_1, val,
					       FIELD_GET(ISO_DCO_1_DCO_CAL_DONE, val), 0, 1000);
}

static u32 dco_get_dco_count_latch(struct clk_dco_data *data)
{
	const struct dco_reg_desc *desc = &dco_reg_desc[data->loc];
	u32 val;

	val = clk_regmap_read(&data->clkr, desc->reg_dco_1);
	return FIELD_GET(ISO_DCO_1_DCO_COUNT_LATCH, val);
}

static u32 dco_get_xtal_count_latch(struct clk_dco_data *data)
{
	const struct dco_reg_desc *desc = &dco_reg_desc[data->loc];
	u32 val;

	val = clk_regmap_read(&data->clkr, desc->reg_dco_1);
	return FIELD_GET(ISO_DCO_1_XTAL_COUNT_LATCH, val);
}

static int dco_get_dco_cnt(struct clk_dco_data *data, u32 target, u32 *count)
{
	int ret;

	dco_set_dco_sel(data, 0);
	dco_set_osc_count_limit(data, target);
	dco_clear_cal_data(data);

	dco_set_cal_en(data, 0);
	dco_set_cal_en(data, 1);

	ret = dco_wait_cal_done(data);
	if (ret)
		return ret;

	*count = dco_get_dco_count_latch(data);
	return 0;
}

static int dco_get_xtal_cnt(struct clk_dco_data *data, u32 target, u32 *count)
{
	int ret;

	dco_set_dco_sel(data, 1);
	dco_set_osc_count_limit(data, target);
	dco_clear_cal_data(data);

	dco_set_cal_en(data, 0);
	dco_set_cal_en(data, 1);

	ret = dco_wait_cal_done(data);
	if (ret)
		return ret;

	*count = dco_get_xtal_count_latch(data);

	return 0;
}

static int dco_calibrate(struct clk_dco_data *data, u32 sig, u32 target, u32 *dco_count)
{
	int retry_left = 30;
	u32 v;

	pll_etn_osc_set_osc_sig(data->pll_etn_osc, sig);

	dco_set_xtal_off(data, 0);
	while (retry_left-- > 0 && dco_get_dco_cnt(data, target, &v))
		pll_etn_osc_set_osc_rstb(data->pll_etn_osc);

	if (dco_count)
		*dco_count = v;

	return retry_left < 0 ? -ETIMEDOUT : 0;
}

static int evaluate_sig_delta(u32 target, u32 cnt)
{
	u32 v;

	v = 10000 * cnt / target;

	if (v > 10074)
		return -1;
	if (v < 10000)
		return 1;
	return 0;
}

static int __dco_recalibrate(struct clk_dco_data *data, u32 target, u32 *dco)
{
	u32 dco_cnt = 0;
	int retry_left = 500;
	int ret;
	u32 sig = 0x40;
	int delta = 0;

	do {
		ret = dco_calibrate(data, sig, target, &dco_cnt);
		if (ret)
			return ret;

		delta = evaluate_sig_delta(target, dco_cnt);
		if (delta == 0) {
			*dco = dco_cnt;
			return 0;
		}

		pr_debug("%pC: %s: sig=%02x, dco_count=%03x, delta=%d\n",
			data->clkr.hw.clk, __func__, sig, dco_cnt, delta);

		sig += delta;

	} while (retry_left-- > 0);

	dco_calibrate(data, 0x40, target, NULL);
	return -EINVAL;
}

static int dco_get_xtal_cnt_locked(struct clk_dco_data *data, u32 target)
{
	unsigned long flags;
	u32 count;
	int ret;

	/* if next calibration starts, ignore this */
	ret = spin_trylock_irqsave(&dco_lock, flags);
	if (!ret)
		return -EBUSY;

	ret = dco_get_xtal_cnt(data, target, &count);
	spin_unlock_irqrestore(&dco_lock, flags);
	return ret ?: count;
}

static int dco_recalibrate(struct clk_dco_data *data)
{
	int ret;
	unsigned long flags;
	u32 target = 0x800;
	u32 dco;
	ktime_t s;

	if (!data->pll_etn_osc)
		return -EINVAL;

	s = ktime_get();
	spin_lock_irqsave(&dco_lock, flags);
	ret = __dco_recalibrate(data, target, &dco);
	spin_unlock_irqrestore(&dco_lock, flags);

	pr_debug("%pC: calibrate takes %lld us\n", data->clkr.hw.clk,
		 ktime_to_us(ktime_sub(ktime_get(), s)));
	pr_debug("%pC: sig=%02x, dco=%03x, errno=%d, (xtal=%03x)\n", data->clkr.hw.clk,
		 pll_etn_osc_get_osc_sig(data->pll_etn_osc), dco, ret,
		 dco_get_xtal_cnt_locked(data, target));

	return ret;
}

static int clk_dco_prepare(struct clk_hw *hw)
{
	struct clk_dco_data *data = to_clk_dco(hw);

	return dco_recalibrate(data);
}

static void clk_dco_unprepare(struct clk_hw *hw)
{}

const struct clk_ops clk_dco_ops = {
	.prepare = clk_dco_prepare,
	.unprepare = clk_dco_unprepare,
};
EXPORT_SYMBOL_GPL(clk_dco_ops);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Cheng-Yu Lee <cylee12@realtek.com>");
