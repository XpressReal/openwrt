/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2016-2019 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */

#ifndef __CLK_REALTEK_COMMON_H
#define __CLK_REALTEK_COMMON_H

#include <linux/bitops.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/hwspinlock.h>
#include "clk-regmap.h"
#include "clk-regmap-gate.h"
#include "clk-regmap-mux.h"
#include "clk-pll.h"
#include "clk-regmap-clkdet.h"
#include "clk-dco.h"
#include "reset.h"

struct device;
struct platform_device;

/* ofs check */
#define CLK_OFS_INVALID                 (-1)
#define CLK_OFS_IS_VALID(_ofs)          ((_ofs) != CLK_OFS_INVALID)

struct freq_table {
	u32 val;
	unsigned long rate;
};

#define FREQ_TABLE_END                  { .rate = 0 }
#define IS_FREQ_TABLE_END(_f)           ((_f)->rate == 0)

struct div_table {
	unsigned long rate;
	u32 div;
	u32 val;
};

#define DIV_TABLE_END                   { .rate = 0 }
#define IS_DIV_TABLE_END(_d)            ((_d)->rate == 0)

const struct freq_table *ftbl_find_by_rate(const struct freq_table *ftbl,
					   unsigned long rate);
const struct freq_table *ftbl_find_by_val_with_mask(const struct freq_table *ftbl,
						    u32 mask, u32 value);
const struct div_table *dtbl_find_by_rate(const struct div_table *dtbl, unsigned long rate);
const struct div_table *dtbl_find_by_val(const struct div_table *dtbl, u32 val);

struct rtk_clk_desc {
	struct clk_hw_onecell_data *clk_data;
	struct clk_regmap **clks;
	size_t num_clks;
	struct rtk_reset_bank *reset_banks;
	size_t num_reset_banks;
};

int rtk_clk_probe(struct platform_device *pdev, const struct rtk_clk_desc *desc);

#endif /* __CLK_REALTEK_COMMON_H */

