// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017,2019 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */

#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <dt-bindings/clock/rtd1295-clk.h>
#include "common.h"

#define DIV_DV(_r, _d, _v)    { .rate = _r, .div = _d, .val = _v, }
#define FREQ_NF_MASK          (0x7FFFF)
#define FREQ_NF(_r, _n, _f)   { .rate = _r, .val = ((_n) << 11) | (_f), }
#define FREQ_MNO_MASK         (0x63FF0)
#define FREQ_MNO(_r, _m, _n, _o) \
	{  .rate = _r, .val = ((_m) << 4) | ((_n) << 12) | ((_o) << 17), }


static const struct div_table scpu_div_tbl[] = {
	DIV_DV(1000000000, 1, 0),
	DIV_DV(500000000,  2, 2),
	DIV_DV(250000000,  4, 3),
	DIV_TABLE_END
};

static const struct freq_table scpu_tbl[] = {
	FREQ_NF(1000000000, 34,   75),
	FREQ_NF(1100000000, 37, 1517),
	FREQ_NF(1200000000, 41,  910),
	FREQ_NF(1300000000, 45,  303),
	FREQ_NF(1400000000, 48, 1745),
	FREQ_NF(1500000000, 52, 1137),
	FREQ_NF(1600000000, 56,  531),
	FREQ_NF(1800000000, 63, 1365),
	FREQ_NF(1200000000, 41, 1024),
	FREQ_NF(1300000000, 45, 1024),
	FREQ_NF(1503000000, 48, 1744),
	FREQ_TABLE_END
};

static struct clk_pll_div pll_scpu = {
	.div_ofs    = 0x030,
	.div_shift  = 7,
	.div_width  = 2,
	.div_tbl    = scpu_div_tbl,
	.clkp       = {
		.pll_type     = CLK_PLL_TYPE_NF_SSC,
		.freq_mask    = FREQ_NF_MASK,
		.flags        = CLK_PLL_DIV_WORKAROUND,
		.ssc_ofs      = 0x500,
		.pll_ofs      = CLK_OFS_INVALID,
		.freq_tbl     = scpu_tbl,
		.clkr.hw.init = CLK_HW_INIT("pll_scpu", "osc27m", &clk_pll_div_ops, CLK_IGNORE_UNUSED | CLK_GET_RATE_NOCACHE),
	},
};

static const struct freq_table bus_tbl[] = {
	FREQ_NF(200000000,  4,  835),
	FREQ_NF(243000000,  6,    0),
	FREQ_NF(256000000,  6, 1024),
	FREQ_NF(256000000,  6,  986),
	FREQ_NF(257000000,  6, 1061),
	FREQ_NF(459000000, 14,    0),
	FREQ_NF(486000000, 15,    0),
	FREQ_NF(482000000, 14, 1744),
	FREQ_NF(513000000, 16,    0),
	FREQ_NF(540000000, 17,    0),
	FREQ_TABLE_END
};

static struct clk_pll pll_bus = {
	.pll_type     = CLK_PLL_TYPE_NF_SSC,
	.freq_mask    = FREQ_NF_MASK,
	.freq_tbl     = bus_tbl,
	.pll_ofs      = CLK_OFS_INVALID,
	.ssc_ofs      = 0x520,
	.clkr.hw.init = CLK_HW_INIT("pll_bus", "osc27m", &clk_pll_ro_ops, CLK_GET_RATE_NOCACHE),
};

static CLK_FIXED_FACTOR(clk_sys, "clk_sys", "pll_bus", 2, 1, 0);

static struct clk_pll pll_dcsb = {
	.pll_type     = CLK_PLL_TYPE_NF_SSC,
	.freq_mask    = FREQ_NF_MASK,
	.freq_tbl     = bus_tbl,
	.pll_ofs      = CLK_OFS_INVALID,
	.ssc_ofs      = 0x540,
	.clkr.hw.init = CLK_HW_INIT("pll_dcsb", "osc27m", &clk_pll_ro_ops, CLK_GET_RATE_NOCACHE),
};

static CLK_FIXED_FACTOR(clk_sysh, "clk_sysh", "pll_dcsb", 1, 1, 0);

static const struct freq_table ddsx_tbl[] = {
	FREQ_NF(432000000, 13, 0),
	FREQ_TABLE_END
};

static struct clk_pll pll_ddsa = {
	.pll_type     = CLK_PLL_TYPE_NF_SSC,
	.freq_mask    = FREQ_NF_MASK,
	.freq_tbl     = ddsx_tbl,
	.pll_ofs      = 0x120,
	.pow_loc      = CLK_PLL_CONF_POW_LOC_CTL3,
	.ssc_ofs      = 0x560,
	.clkr.hw.init = CLK_HW_INIT("pll_ddsa", "osc27m", &clk_pll_ops, CLK_IGNORE_UNUSED | CLK_GET_RATE_NOCACHE),
};

static struct clk_pll pll_ddsb = {
	.pll_type     = CLK_PLL_TYPE_NF_SSC,
	.freq_mask    = FREQ_NF_MASK,
	.freq_tbl     = ddsx_tbl,
	.pll_ofs      = 0x174,
	.pow_loc      = CLK_PLL_CONF_POW_LOC_CTL2,
	.ssc_ofs      = 0x580,
	.clkr.hw.init = CLK_HW_INIT("pll_ddsb", "osc27m", &clk_pll_ops, CLK_GET_RATE_NOCACHE),
};

static const struct freq_table gpu_tbl[] = {
	FREQ_NF(300000000,  8,  227),
	FREQ_NF(320000000,  8, 1744),
	FREQ_NF(340000000,  9, 1213),
	FREQ_NF(360000000, 10,  682),
	FREQ_NF(380000000, 11,  151),
	FREQ_NF(400000000, 11, 1668),
	FREQ_NF(420000000, 12, 1137),
	FREQ_NF(440000000, 13,  606),
	FREQ_NF(460000000, 14,   75),
	FREQ_NF(480000000, 14, 1592),
	FREQ_NF(500000000, 15, 1061),
	FREQ_NF(520000000, 16,  530),
	FREQ_NF(540000000, 17,    0),
	FREQ_NF(560000000, 17, 1517),
	FREQ_NF(580000000, 18,  986),
	FREQ_NF(600000000, 19,  455),
	FREQ_NF(620000000, 19, 1972),
	FREQ_NF(640000000, 20, 1441),
	FREQ_NF(660000000, 21,  910),
	FREQ_NF(680000000, 22,  379),
	FREQ_NF(460000000, 13, 1365),
	FREQ_TABLE_END
};

static struct clk_pll pll_gpu = {
	.pll_type     = CLK_PLL_TYPE_NF_SSC,
	.freq_mask    = FREQ_NF_MASK,
	.freq_tbl     = gpu_tbl,
	.pll_ofs      = 0x1C0,
	.pow_loc      = CLK_PLL_CONF_POW_LOC_CTL2,
	.ssc_ofs      = 0x5A0,
	.clkr.hw.init = CLK_HW_INIT("pll_gpu", "osc27m", &clk_pll_ops, CLK_GET_RATE_NOCACHE),
};

static const struct freq_table ve_tbl[] = {
	FREQ_MNO(189000000,  5, 0, 0),
	FREQ_MNO(270000000,  8, 0, 0),
	FREQ_MNO(405000000, 13, 0, 0),
	FREQ_MNO(432000000, 14, 0, 0),
	FREQ_MNO(459000000, 15, 0, 0),
	FREQ_MNO(486000000, 16, 0, 0),
	FREQ_MNO(513000000, 17, 0, 0),
	FREQ_MNO(540000000, 18, 0, 0),
	FREQ_MNO(567000000, 19, 0, 0),
	FREQ_MNO(594000000, 20, 0, 0),
	FREQ_MNO(648000000, 22, 0, 0),
	FREQ_MNO(675000000, 23, 0, 0),
	FREQ_MNO(702000000, 24, 0, 0),
	FREQ_MNO(715000000, 51, 1, 0),
	FREQ_TABLE_END
};

static struct clk_pll pll_ve1 = {
	.pll_type     = CLK_PLL_TYPE_MNO,
	.freq_mask    = FREQ_MNO_MASK,
	.freq_tbl     = ve_tbl,
	.pll_ofs      = 0x114,
	.pow_loc      = CLK_PLL_CONF_POW_LOC_CTL2,
	.ssc_ofs      = CLK_OFS_INVALID,
	.clkr.hw.init = CLK_HW_INIT("pll_ve1", "osc27m", &clk_pll_ops, CLK_GET_RATE_NOCACHE | CLK_SET_RATE_GATE),
};

static struct clk_pll pll_ve2 = {
	.pll_type     = CLK_PLL_TYPE_MNO,
	.freq_mask    = FREQ_MNO_MASK,
	.freq_tbl     = ve_tbl,
	.pll_ofs      = 0x1D0,
	.pow_loc      = CLK_PLL_CONF_POW_LOC_CTL2,
	.ssc_ofs      = CLK_OFS_INVALID,
	.clkr.hw.init = CLK_HW_INIT("pll_ve2", "osc27m", &clk_pll_ops, CLK_GET_RATE_NOCACHE | CLK_SET_RATE_GATE),
};


static const char * const ve_parents[] = {
	"clk_sysh",
	"pll_ve1",
	"pll_ve2",
	"pll_ve2",
};

static struct clk_regmap_gate clk_en_misc = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_misc", &clk_regmap_gate_ops, CLK_IS_CRITICAL),
	.gate_ofs = 0xc,
	.bit_idx = 0,
};

static struct clk_regmap_gate clk_en_pcie0 = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_pcie0", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0xc,
	.bit_idx = 1,
};

static struct clk_regmap_gate clk_en_sata_0 = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_sata_0", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0xc,
	.bit_idx = 2,
};

static struct clk_regmap_gate clk_en_gspi = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_gspi", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0xc,
	.bit_idx = 3,
};

static struct clk_regmap_gate clk_en_usb = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_usb", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0xc,
	.bit_idx = 4,
};

static struct clk_regmap_gate clk_en_sata_alive_0 = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_sata_alive_0", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0xc,
	.bit_idx = 7,
};

static struct clk_regmap_gate clk_en_hdmi = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_hdmi", &clk_regmap_gate_ops, CLK_IGNORE_UNUSED),
	.gate_ofs = 0xc,
	.bit_idx = 8,
};

static struct clk_regmap_gate clk_en_etn = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_etn", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0xc,
	.bit_idx = 9,
};

static struct clk_regmap_gate clk_en_gpu = {
	.clkr.hw.init = CLK_HW_INIT("clk_en_gpu", "pll_gpu", &clk_regmap_gate_ops, CLK_SET_RATE_PARENT),
	.gate_ofs = 0xc,
	.bit_idx = 11,
};

static struct clk_regmap_gate clk_en_ve1 = {
	.clkr.hw.init = CLK_HW_INIT("clk_en_ve1", "clk_ve1", &clk_regmap_gate_ops, CLK_SET_RATE_PARENT),
	.gate_ofs = 0xc,
	.bit_idx = 12,
};

static struct clk_regmap_gate clk_en_ve2 = {
	.clkr.hw.init = CLK_HW_INIT("clk_en_ve2", "clk_ve2", &clk_regmap_gate_ops, CLK_SET_RATE_PARENT),
	.gate_ofs = 0xc,
	.bit_idx = 13,
};

static struct clk_regmap_gate clk_en_tve = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_tve", &clk_regmap_gate_ops, CLK_IGNORE_UNUSED),
	.gate_ofs = 0xc,
	.bit_idx = 14,
};

static struct clk_regmap_gate clk_en_vo = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_vo", &clk_regmap_gate_ops, CLK_IGNORE_UNUSED),
	.gate_ofs = 0xc,
	.bit_idx = 15,
};

static struct clk_regmap_gate clk_en_lvds = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_lvds", &clk_regmap_gate_ops, CLK_IGNORE_UNUSED),
	.gate_ofs = 0xc,
	.bit_idx = 16,
};

static struct clk_regmap_gate clk_en_se = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_se", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0xc,
	.bit_idx = 17,
};

static struct clk_regmap_gate clk_en_cp = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_cp", &clk_regmap_gate_ops, CLK_IGNORE_UNUSED),
	.gate_ofs = 0xc,
	.bit_idx = 19,
};

static struct clk_regmap_gate clk_en_md = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_md", &clk_regmap_gate_ops, CLK_IGNORE_UNUSED),
	.gate_ofs = 0xc,
	.bit_idx = 20,
};

static struct clk_regmap_gate clk_en_tp = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_tp", &clk_regmap_gate_ops, CLK_IGNORE_UNUSED),
	.gate_ofs = 0xc,
	.bit_idx = 21,
};

static struct clk_regmap_gate clk_en_nf = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_nf", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0xc,
	.bit_idx = 23,
};

static struct clk_regmap_gate clk_en_emmc = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_emmc", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0xc,
	.bit_idx = 24,
};

static struct clk_regmap_gate clk_en_cr = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_cr", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0xc,
	.bit_idx = 25,
};

static struct clk_regmap_gate clk_en_sdio_ip = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_sdio_ip", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0xc,
	.bit_idx = 26,
};

static struct clk_regmap_gate clk_en_mipi = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_mipi", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0xc,
	.bit_idx = 27,
};

static struct clk_regmap_gate clk_en_emmc_ip = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_emmc_ip", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0xc,
	.bit_idx = 28,
};

static struct clk_regmap_gate clk_en_ve3 = {
	.clkr.hw.init = CLK_HW_INIT("clk_en_ve3", "clk_ve3", &clk_regmap_gate_ops, CLK_SET_RATE_PARENT),
	.gate_ofs = 0xc,
	.bit_idx = 29,
};

static struct clk_regmap_gate clk_en_sdio = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_sdio", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0xc,
	.bit_idx = 30,
};

static struct clk_regmap_gate clk_en_sd_ip = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_sd_ip", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0xc,
	.bit_idx = 31,
};

static struct clk_regmap_gate clk_en_nat = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_nat", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x10,
	.bit_idx = 0,
};

static struct clk_regmap_gate clk_en_i2c5 = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_i2c5", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x10,
	.bit_idx = 1,
};

static struct clk_regmap_gate clk_en_jpeg = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_jpeg", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x10,
	.bit_idx = 3,
};

static struct clk_regmap_gate clk_en_pcie1 = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_pcie1", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x10,
	.bit_idx = 5,
};

static struct clk_regmap_gate clk_en_sc = {
	.clkr.hw.init = CLK_HW_INIT("clk_en_sc", "misc", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x10,
	.bit_idx = 6,
};

static struct clk_regmap_gate clk_en_cbus_tx = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_cbus_tx", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x10,
	.bit_idx = 7,
};

static struct clk_regmap_gate clk_en_rtc = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_rtc", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x10,
	.bit_idx = 10,
};

static struct clk_regmap_gate clk_en_i2c4 = {
	.clkr.hw.init = CLK_HW_INIT("clk_en_i2c4", "misc", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x10,
	.bit_idx = 13,
};

static struct clk_regmap_gate clk_en_i2c3 = {
	.clkr.hw.init = CLK_HW_INIT("clk_en_i2c3", "misc", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x10,
	.bit_idx = 14,
};

static struct clk_regmap_gate clk_en_i2c2 = {
	.clkr.hw.init = CLK_HW_INIT("clk_en_i2c2", "misc", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x10,
	.bit_idx = 15,
};

static struct clk_regmap_gate clk_en_hdmirx = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_hdmirx", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x10,
	.bit_idx = 24,
};

static struct clk_regmap_gate clk_en_sata_1 = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_sata_1", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x10,
	.bit_idx = 25,
};

static struct clk_regmap_gate clk_en_sata_alive_1 = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_sata_alive_1", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x10,
	.bit_idx = 26,
};

static struct clk_regmap_gate clk_en_ur2 = {
	.clkr.hw.init = CLK_HW_INIT("clk_en_ur2", "pll_ddsa", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x10,
	.bit_idx = 27,
};

static struct clk_regmap_gate clk_en_ur1 = {
	.clkr.hw.init = CLK_HW_INIT("clk_en_ur1", "pll_ddsa", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x10,
	.bit_idx = 28,
};

static struct clk_regmap_gate clk_en_fan = {
	.clkr.hw.init = CLK_HW_INIT("clk_en_fan", "misc", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x10,
	.bit_idx = 29,
};

static struct clk_regmap_gate clk_en_lsadc = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_lsadc", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x450,
	.bit_idx = 2,
};

static struct clk_regmap_mux clk_ve1 = {
	.clkr.hw.init = CLK_HW_INIT_PARENTS("clk_ve1", ve_parents, &clk_regmap_mux_ops, CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT),
	.mux_ofs = 0x4c,
	.shift = 0,
	.mask = 0x3,
};

static struct clk_regmap_mux clk_ve2 = {
	.clkr.hw.init = CLK_HW_INIT_PARENTS("clk_ve2", ve_parents, &clk_regmap_mux_ops, CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT),
	.mux_ofs = 0x4c,
	.shift = 2,
	.mask = 0x3,
};

static struct clk_regmap_mux clk_ve3 = {
	.clkr.hw.init = CLK_HW_INIT_PARENTS("clk_ve3", ve_parents, &clk_regmap_mux_ops, CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT),
	.mux_ofs = 0x4c,
	.shift = 4,
	.mask = 0x3,
};

static struct clk_regmap *rtd1295_cc_regmap_clks[] = {
	&pll_dcsb.clkr,
	&pll_ddsa.clkr,
	&pll_ddsb.clkr,
	&pll_gpu.clkr,
	&pll_ve1.clkr,
	&pll_ve2.clkr,
	&clk_ve1.clkr,
	&clk_ve2.clkr,
	&clk_ve3.clkr,
	&pll_scpu.clkp.clkr,
	&pll_bus.clkr,
	&clk_en_misc.clkr,
	&clk_en_pcie0.clkr,
	&clk_en_sata_0.clkr,
	&clk_en_gspi.clkr,
	&clk_en_usb.clkr,
	&clk_en_sata_alive_0.clkr,
	&clk_en_hdmi.clkr,
	&clk_en_etn.clkr,
	&clk_en_gpu.clkr,
	&clk_en_ve1.clkr,
	&clk_en_ve2.clkr,
	&clk_en_tve.clkr,
	&clk_en_vo.clkr,
	&clk_en_lvds.clkr,
	&clk_en_se.clkr,
	&clk_en_cp.clkr,
	&clk_en_md.clkr,
	&clk_en_tp.clkr,
	&clk_en_nf.clkr,
	&clk_en_emmc.clkr,
	&clk_en_cr.clkr,
	&clk_en_sdio_ip.clkr,
	&clk_en_mipi.clkr,
	&clk_en_emmc_ip.clkr,
	&clk_en_ve3.clkr,
	&clk_en_sdio.clkr,
	&clk_en_sd_ip.clkr,
	&clk_en_nat.clkr,
	&clk_en_i2c5.clkr,
	&clk_en_jpeg.clkr,
	&clk_en_pcie1.clkr,
	&clk_en_sc.clkr,
	&clk_en_cbus_tx.clkr,
	&clk_en_rtc.clkr,
	&clk_en_i2c4.clkr,
	&clk_en_i2c3.clkr,
	&clk_en_i2c2.clkr,
	&clk_en_hdmirx.clkr,
	&clk_en_sata_1.clkr,
	&clk_en_sata_alive_1.clkr,
	&clk_en_ur2.clkr,
	&clk_en_ur1.clkr,
	&clk_en_fan.clkr,
	&clk_en_lsadc.clkr,
};

static struct clk_hw_onecell_data rtd1295_cc_hw_data = {
	.num = RTD1295_CRT_CLK_MAX,
	.hws = {
		[RTD1295_CRT_PLL_SCPU] = &__clk_pll_div_hw(&pll_scpu),
		[RTD1295_CRT_PLL_BUS] = &__clk_pll_hw(&pll_bus),
		[RTD1295_CRT_CLK_SYS] = &clk_sys.hw,
		[RTD1295_CRT_PLL_DCSB] = &__clk_pll_hw(&pll_dcsb),
		[RTD1295_CRT_CLK_SYSH] = &clk_sysh.hw,
		[RTD1295_CRT_PLL_DDSA] = &__clk_pll_hw(&pll_ddsa),
		[RTD1295_CRT_PLL_DDSB] = &__clk_pll_hw(&pll_ddsb),
		[RTD1295_CRT_PLL_GPU] = &__clk_pll_hw(&pll_gpu),
		[RTD1295_CRT_PLL_VE1] = &__clk_pll_hw(&pll_ve1),
		[RTD1295_CRT_PLL_VE2] = &__clk_pll_hw(&pll_ve2),
		[RTD1295_CRT_CLK_VE1] = &__clk_regmap_mux_hw(&clk_ve1),
		[RTD1295_CRT_CLK_VE2] = &__clk_regmap_mux_hw(&clk_ve2),
		[RTD1295_CRT_CLK_VE3] = &__clk_regmap_mux_hw(&clk_ve3),
		[RTD1295_CRT_CLK_EN_MISC] = &__clk_regmap_gate_hw(&clk_en_misc),
		[RTD1295_CRT_CLK_EN_PCIE0] = &__clk_regmap_gate_hw(&clk_en_pcie0),
		[RTD1295_CRT_CLK_EN_SATA_0] = &__clk_regmap_gate_hw(&clk_en_sata_0),
		[RTD1295_CRT_CLK_EN_GSPI] = &__clk_regmap_gate_hw(&clk_en_gspi),
		[RTD1295_CRT_CLK_EN_USB] = &__clk_regmap_gate_hw(&clk_en_usb),
		[RTD1295_CRT_CLK_EN_SATA_ALIVE_0] = &__clk_regmap_gate_hw(&clk_en_sata_alive_0),
		[RTD1295_CRT_CLK_EN_HDMI] = &__clk_regmap_gate_hw(&clk_en_hdmi),
		[RTD1295_CRT_CLK_EN_ETN] = &__clk_regmap_gate_hw(&clk_en_etn),
		[RTD1295_CRT_CLK_EN_GPU] = &__clk_regmap_gate_hw(&clk_en_gpu),
		[RTD1295_CRT_CLK_EN_VE1] = &__clk_regmap_gate_hw(&clk_en_ve1),
		[RTD1295_CRT_CLK_EN_VE2] = &__clk_regmap_gate_hw(&clk_en_ve2),
		[RTD1295_CRT_CLK_EN_TVE] = &__clk_regmap_gate_hw(&clk_en_tve),
		[RTD1295_CRT_CLK_EN_VO] = &__clk_regmap_gate_hw(&clk_en_vo),
		[RTD1295_CRT_CLK_EN_LVDS] = &__clk_regmap_gate_hw(&clk_en_lvds),
		[RTD1295_CRT_CLK_EN_SE] = &__clk_regmap_gate_hw(&clk_en_se),
		[RTD1295_CRT_CLK_EN_CP] = &__clk_regmap_gate_hw(&clk_en_cp),
		[RTD1295_CRT_CLK_EN_MD] = &__clk_regmap_gate_hw(&clk_en_md),
		[RTD1295_CRT_CLK_EN_TP] = &__clk_regmap_gate_hw(&clk_en_tp),
		[RTD1295_CRT_CLK_EN_NF] = &__clk_regmap_gate_hw(&clk_en_nf),
		[RTD1295_CRT_CLK_EN_EMMC] = &__clk_regmap_gate_hw(&clk_en_emmc),
		[RTD1295_CRT_CLK_EN_CR] = &__clk_regmap_gate_hw(&clk_en_cr),
		[RTD1295_CRT_CLK_EN_SDIO_IP] = &__clk_regmap_gate_hw(&clk_en_sdio_ip),
		[RTD1295_CRT_CLK_EN_MIPI] = &__clk_regmap_gate_hw(&clk_en_mipi),
		[RTD1295_CRT_CLK_EN_EMMC_IP] = &__clk_regmap_gate_hw(&clk_en_emmc_ip),
		[RTD1295_CRT_CLK_EN_VE3] = &__clk_regmap_gate_hw(&clk_en_ve3),
		[RTD1295_CRT_CLK_EN_SDIO] = &__clk_regmap_gate_hw(&clk_en_sdio),
		[RTD1295_CRT_CLK_EN_SD_IP] = &__clk_regmap_gate_hw(&clk_en_sd_ip),
		[RTD1295_CRT_CLK_EN_NAT] = &__clk_regmap_gate_hw(&clk_en_nat),
		[RTD1295_CRT_CLK_EN_MISC_I2C_5] = &__clk_regmap_gate_hw(&clk_en_i2c5),
		[RTD1295_CRT_CLK_EN_JPEG] = &__clk_regmap_gate_hw(&clk_en_jpeg),
		[RTD1295_CRT_CLK_EN_PCIE1] = &__clk_regmap_gate_hw(&clk_en_pcie1),
		[RTD1295_CRT_CLK_EN_MISC_SC] = &__clk_regmap_gate_hw(&clk_en_sc),
		[RTD1295_CRT_CLK_EN_CBUS_TX] = &__clk_regmap_gate_hw(&clk_en_cbus_tx),
		[RTD1295_CRT_CLK_EN_MISC_RTC] = &__clk_regmap_gate_hw(&clk_en_rtc),
		[RTD1295_CRT_CLK_EN_MISC_I2C_4] = &__clk_regmap_gate_hw(&clk_en_i2c4),
		[RTD1295_CRT_CLK_EN_MISC_I2C_3] = &__clk_regmap_gate_hw(&clk_en_i2c3),
		[RTD1295_CRT_CLK_EN_MISC_I2C_2] = &__clk_regmap_gate_hw(&clk_en_i2c2),
		[RTD1295_CRT_CLK_EN_HDMIRX] = &__clk_regmap_gate_hw(&clk_en_hdmirx),
		[RTD1295_CRT_CLK_EN_SATA_1] = &__clk_regmap_gate_hw(&clk_en_sata_1),
		[RTD1295_CRT_CLK_EN_SATA_ALIVE_1] = &__clk_regmap_gate_hw(&clk_en_sata_alive_1),
		[RTD1295_CRT_CLK_EN_UR2] = &__clk_regmap_gate_hw(&clk_en_ur2),
		[RTD1295_CRT_CLK_EN_UR1] = &__clk_regmap_gate_hw(&clk_en_ur1),
		[RTD1295_CRT_CLK_EN_FAN] = &__clk_regmap_gate_hw(&clk_en_fan),
		[RTD1295_CRT_CLK_EN_LSADC] = &__clk_regmap_gate_hw(&clk_en_lsadc),
		[RTD1295_CRT_CLK_MAX] = NULL,
	},
};

static struct rtk_reset_bank rtd1295_cc_reset_banks[] = {
	{ .ofs = 0x00, },
	{ .ofs = 0x04, },
	{ .ofs = 0x50, },
};

static const struct rtk_clk_desc rtd1295_cc_desc = {
	.clk_data = &rtd1295_cc_hw_data,
	.clks = rtd1295_cc_regmap_clks,
	.num_clks = ARRAY_SIZE(rtd1295_cc_regmap_clks),
	.reset_banks = rtd1295_cc_reset_banks,
	.num_reset_banks = ARRAY_SIZE(rtd1295_cc_reset_banks),
};

static int rtd1295_cc_probe(struct platform_device *pdev)
{
	return rtk_clk_probe(pdev, &rtd1295_cc_desc);
}

static const struct of_device_id rtd1295_cc_match[] = {
	{ .compatible = "realtek,rtd1295-crt-clk", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rtd1295_cc_match);

static struct platform_driver rtd1295_cc_driver = {
	.probe = rtd1295_cc_probe,
	.driver = {
		.name = "rtk-rtd1295-crt-clk",
		.of_match_table = of_match_ptr(rtd1295_cc_match),
	},
};

static int __init rtd1295_cc_init(void)
{
	return platform_driver_register(&rtd1295_cc_driver);
}
subsys_initcall(rtd1295_cc_init);
MODULE_DESCRIPTION("Reatek RTD1295 CRT Controller Driver");
MODULE_AUTHOR("Cheng-Yu Lee <cylee12@realtek.com>");
MODULE_LICENSE("GPL v2");
