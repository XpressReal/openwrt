/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 Realtek Semiconductor Corp.
 */

#ifndef __CLK_REALTEK_CLK_DCO_H
#define __CLK_REALTEK_CLK_DCO_H

#include <linux/clk-provider.h>
#include <linux/types.h>
#include "clk-regmap.h"

struct clk_dco_data {
	struct clk_regmap clkr;
	u32 loc;
	struct clk_regmap *pll_etn_osc;
};

#define to_clk_dco(_hw) container_of(_hw, struct clk_dco_data, clkr.hw)
#define __clk_dco_hw(_ptr)  ((_ptr)->clkr.hw)

extern const struct clk_ops clk_dco_ops;

#endif
