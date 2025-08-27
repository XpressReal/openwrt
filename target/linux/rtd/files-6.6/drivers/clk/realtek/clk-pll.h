/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2019 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */

#ifndef __CLK_REALTEK_CLK_PLL_H
#define __CLK_REALTEK_CLK_PLL_H

#include "clk-regmap.h"

struct clk_pll {
	struct clk_regmap clkr;
	u32 pll_type;
#define CLK_PLL_TYPE_MNO         1
#define CLK_PLL_TYPE_NF          2
#define CLK_PLL_TYPE_NF_SSC      3
#define CLK_PLL_TYPE_MNO_V2      4

	int pll_ofs;
	int ssc_ofs;

	const struct freq_table *freq_tbl;
	u32 freq_mask;
	u32 freq_mask_set;
	u32 freq_val;

	u32 pow_loc;
#define CLK_PLL_CONF_NO_POW             0
#define CLK_PLL_CONF_POW_LOC_CTL2       1
#define CLK_PLL_CONF_POW_LOC_CTL3       2
	u32 pow_set_rs : 1;
	u32 pow_set_pi_bps : 1;
	u32 pow_set_analog : 1;
	u32 rs_mask;
	u32 rs_val;
	u32 analog_mask;
	u32 analog_val;

	spinlock_t *lock;

	u32 flags;
};

#define to_clk_pll(_hw) container_of(to_clk_regmap(_hw), struct clk_pll, clkr)
#define __clk_pll_hw(_ptr)  __clk_regmap_hw(&(_ptr)->clkr)

/* clk_pll flags */
#define CLK_PLL_DIV_WORKAROUND          BIT(2)

static inline u32 clk_pll_get_pow_offset(struct clk_pll *clkp)
{
	return (clkp->pow_loc == CLK_PLL_CONF_POW_LOC_CTL3) ? 0x8 : 0x4;
}

static inline u32 clk_pll_get_freq_mask(struct clk_pll *clkp)
{
	return clkp->freq_mask_set ?: clkp->freq_mask;
}

static inline bool clk_pll_has_pow(struct clk_pll *pll)
{
	if (pll->pow_loc != CLK_PLL_CONF_NO_POW)
		return true;
	return false;
}

static inline unsigned long clk_pll_lock(struct clk_pll *pll)
{
	unsigned long flags = 0;

	if (pll->lock)
		spin_lock_irqsave(pll->lock, flags);
	return flags;
}

static inline void clk_pll_unlock(struct clk_pll *pll, unsigned long flags)
{
	if (pll->lock)
		spin_unlock_irqrestore(pll->lock, flags);
}

struct clk_pll_div {
	struct clk_pll clkp;
	int div_ofs;
	int div_shift;
	int div_width;
	const struct div_table *div_tbl;
	spinlock_t *lock;
};

#define to_clk_pll_div(_hw) \
	container_of(to_clk_pll(_hw), struct clk_pll_div, clkp)
#define __clk_pll_div_hw(_ptr) __clk_pll_hw(&(_ptr)->clkp)

/* clk_pll_div helper functions */
static inline unsigned long clk_pll_div_lock(struct clk_pll_div *plld)
{
	unsigned long flags = 0;

	if (plld->lock)
		spin_lock_irqsave(plld->lock, flags);
	return flags;
}

static inline void clk_pll_div_unlock(struct clk_pll_div *plld,
	unsigned long flags)
{
	if (plld->lock)
		spin_unlock_irqrestore(plld->lock, flags);
}

extern const struct clk_ops clk_pll_ops;
extern const struct clk_ops clk_pll_ro_ops;
extern const struct clk_ops clk_pll_div_ops;

struct clk_pll_psaud {
	struct clk_regmap clkr;
	int id;
#define CLK_PLL_PSAUD1A       (0x1)
#define CLK_PLL_PSAUD2A       (0x2)
	int reg;
	spinlock_t *lock;
};


#define to_clk_pll_psaud(_hw) \
	container_of(to_clk_regmap(_hw), struct clk_pll_psaud, clkr)
#define __clk_pll_psaud_hw(_ptr)  __clk_regmap_hw(&(_ptr)->clkr)

struct clk_pll_dif {
	struct clk_regmap clkr;
	int pll_ofs;
	int ssc_ofs;
	u32 status;
	spinlock_t *lock;
	u32 adtv_conf[8];
	int freq;
};
#define to_clk_pll_dif(_hw) \
	container_of(to_clk_regmap(_hw), struct clk_pll_dif, clkr)
#define __clk_pll_dif_hw(_ptr)  __clk_regmap_hw(&(_ptr)->clkr)

struct clk_pll_mmc {
	struct clk_regmap clkr;
	int pll_ofs;
	int ssc_dig_ofs;

	struct clk_hw phase0_hw;
	struct clk_hw phase1_hw;

	u32 set_rate_val_53_97_set_ipc: 1;
};

#define to_clk_pll_mmc(_hw) \
	container_of(to_clk_regmap(_hw), struct clk_pll_mmc, clkr)
#define __clk_pll_mmc_hw(_ptr)  __clk_regmap_hw(&(_ptr)->clkr)

extern const struct clk_ops clk_pll_psaud_ops;
extern const struct clk_ops clk_pll_dif_ops;
extern const struct clk_ops clk_pll_dif_v2_ops;
extern const struct clk_ops clk_pll_mmc_ops;
extern const struct clk_ops clk_pll_mmc_v2_ops;
extern const struct clk_ops clk_pll_mmc_phase_ops;

#endif /* __CLK_REALTEK_CLK_PLL_H */
