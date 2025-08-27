#ifndef __CLK_REALTEK_CLK_REGMAP_H
#define __CLK_REALTEK_CLK_REGMAP_H

#include <linux/clk-provider.h>
#include <linux/hwspinlock.h>
#include <linux/regmap.h>

struct clk_regmap {
	struct clk_hw hw;
	struct regmap *regmap;
};

#define to_clk_regmap(_hw) container_of(_hw, struct clk_regmap, hw)
#define __clk_regmap_hw(_p) ((_p)->hw)

void clk_regmap_write(struct clk_regmap *clkr, u32 ofs, u32 val);
u32 clk_regmap_read(struct clk_regmap *clkr, u32 ofs);
void clk_regmap_update_bits(struct clk_regmap *clkr, u32 ofs, u32 mask, u32 val);

#endif
