// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2019 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */

#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <dt-bindings/clock/rtd1195-clk.h>
#include "common.h"

#define DIV_DV(_r, _div, _val) { .rate = _r, .div = _div, .val = _val, }
#define FREQ_NF_MASK    0x1FFFFC00
#define FREQ_NF(_r, _n, _f) { .rate = _r, .val = ((_n) << 21) | ((_f) << 10), }
#define FREQ_MNO_MASK   0x030FF800
#define FREQ_MNO(_r, _m, _n, _o) \
	{ .rate = _r, .val = ((_m) << 11) | ((_n) << 18) | ((_o) << 24), }
#define FREQ_MN_MASK    0x07FC0000
#define FREQ_MN(_r, _m, _n) { .rate = _r, .val = ((_m) << 18) | ((_n) << 25), }


static const struct div_table scpu_div_tbl[] = {
	DIV_DV(700000000, 1, 0),
	DIV_DV(350000000, 2, 2),
	DIV_DV(290000000, 4, 3),
	DIV_TABLE_END
};

static const struct freq_table scpu_tbl[] = {
	FREQ_NF(720000000,  25, 1364),
	FREQ_NF(780000000,  27, 1820),
	FREQ_NF(800000000,  28, 1290),
	FREQ_NF(1000000000, 36,   75),
	FREQ_NF(1160000000, 41, 1159),
	FREQ_NF(1200000000, 43,  910),
	FREQ_TABLE_END
};

static struct clk_pll_div pll_scpu = {
	.div_ofs    = 0x030,
	.div_shift  = 7,
	.div_width  = 2,
	.div_tbl    = scpu_div_tbl,
	.clkp       = {
		.pll_type     = CLK_PLL_TYPE_NF,
		.freq_mask    = FREQ_NF_MASK,
		.freq_tbl     = scpu_tbl,
		.pll_ofs      = 0x100,
		.clkr.hw.init = CLK_HW_INIT("pll_scpu", "osc27m", &clk_pll_div_ops, CLK_IGNORE_UNUSED | CLK_GET_RATE_NOCACHE),
	},
};

static const struct div_table bus_div_tbl[] = {
	DIV_DV(250000000, 2, 0),
	DIV_DV(1,         4, 1),
	DIV_TABLE_END
};

static const struct freq_table bus_tbl[] = {
	FREQ_MN(459000000, 15, 0),
	FREQ_TABLE_END
};

static struct clk_pll_div pll_bus = {
	.div_ofs    = 0x030,
	.div_shift  = 0,
	.div_width  = 1,
	.div_tbl    = bus_div_tbl,
	.clkp       = {
		.pll_type     = CLK_PLL_TYPE_MNO,
		.freq_mask    = FREQ_MN_MASK,
		.freq_tbl     = bus_tbl,
		.pll_ofs      = 0x164,
		.pow_loc      = CLK_PLL_CONF_POW_LOC_CTL3,
		.clkr.hw.init = CLK_HW_INIT("pll_bus", "osc27m", &clk_pll_div_ops, CLK_IGNORE_UNUSED | CLK_GET_RATE_NOCACHE),
	},
};

static struct clk_fixed_factor clk_sys = {
	.div     = 1,
	.mult    = 1,
	.hw.init = CLK_HW_INIT("clk_sys", "pll_bus", &clk_fixed_factor_ops, 0),
};

static const struct freq_table dcsb_tbl[] = {
	FREQ_MN(351000000, 11, 0),
	FREQ_TABLE_END
};

static struct clk_pll pll_dcsb = {
	.pll_type  = CLK_PLL_TYPE_MNO,
	.freq_mask = FREQ_MN_MASK,
	.freq_tbl  = dcsb_tbl,
	.pll_ofs   = 0x1B4,
	.pow_loc   = CLK_PLL_CONF_POW_LOC_CTL3,
	.clkr.hw.init = CLK_HW_INIT("pll_dcsb", "osc27m", &clk_pll_ops, CLK_IGNORE_UNUSED | CLK_GET_RATE_NOCACHE),
};

static struct clk_fixed_factor clk_sysh = {
	.div     = 1,
	.mult    = 1,
	.hw.init = CLK_HW_INIT("clk_sysh", "pll_dscb", &clk_fixed_factor_ops, CLK_SET_RATE_PARENT),
};

static const struct freq_table gpu_tbl[] = {
	FREQ_MNO(378000000, 12, 0, 0),
	FREQ_TABLE_END
};

static struct clk_pll pll_gpu = {
	.pll_type  = CLK_PLL_TYPE_MNO,
	.freq_mask = FREQ_MNO_MASK,
	.freq_tbl  = gpu_tbl,
	.pll_ofs   = 0x1C0,
	.pow_loc   = CLK_PLL_CONF_POW_LOC_CTL2,
	.clkr.hw.init = CLK_HW_INIT("pll_gpu", "osc27m", &clk_pll_ops, CLK_GET_RATE_NOCACHE),
};

static const struct freq_table vcpu_tbl[] = {
	FREQ_MNO(243000000,  7, 0, 0),
	FREQ_MNO(324000000, 10, 0, 0),
	FREQ_MNO(405000000, 13, 0, 0),
	FREQ_TABLE_END
};

static struct clk_pll pll_vcpu = {
	.pll_type  = CLK_PLL_TYPE_MNO,
	.freq_mask = FREQ_MNO_MASK,
	.freq_tbl  = vcpu_tbl,
	.pll_ofs   = 0x114,
	.pow_loc   = CLK_PLL_CONF_POW_LOC_CTL2,
	.clkr.hw.init = CLK_HW_INIT("pll_vcpu", "osc27m", &clk_pll_ops, CLK_GET_RATE_NOCACHE),
};

static struct clk_regmap_gate clk_en_misc = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_misc", &clk_regmap_gate_ops, CLK_IS_CRITICAL),
	.gate_ofs = 0xc,
	.bit_idx = 0,
};

static struct clk_regmap_gate clk_en_hdmirx = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_hdmirx", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0xc,
	.bit_idx = 1,
};

static struct clk_regmap_gate clk_en_gspi = {
	.clkr.hw.init = CLK_HW_INIT("clk_en_gspi", "misc", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0xc,
	.bit_idx = 3,
};

static struct clk_regmap_gate clk_en_usb = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_usb", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0xc,
	.bit_idx = 4,
};

static struct clk_regmap_gate clk_en_hdmi = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_hdmi", &clk_regmap_gate_ops, 0),
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

static struct clk_regmap_gate clk_en_jpeg = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_jpeg", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0xc,
	.bit_idx = 13,
};

static struct clk_regmap_gate clk_en_se = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_se", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0xc,
	.bit_idx = 17,
};

static struct clk_regmap_gate clk_en_cp = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_cp", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0xc,
	.bit_idx = 19,
};

static struct clk_regmap_gate clk_en_md = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_md", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0xc,
	.bit_idx = 20,
};

static struct clk_regmap_gate clk_en_tp = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_tp", &clk_regmap_gate_ops, 0),
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

static struct clk_regmap_gate clk_en_ve2 = {
	.clkr.hw.init = CLK_HW_INIT("clk_en_ve2", "clk_ve2", &clk_regmap_gate_ops, CLK_SET_RATE_PARENT),
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

static struct clk_regmap_gate clk_en_i2c5 = {
	.clkr.hw.init = CLK_HW_INIT("clk_en_i2c5", "misc", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x10,
	.bit_idx = 2,
};

static struct clk_regmap_gate clk_en_ve = {
	.clkr.hw.init = CLK_HW_INIT("clk_en_ve", "pll_vcpu", &clk_regmap_gate_ops, CLK_SET_RATE_PARENT),
	.gate_ofs = 0x10,
	.bit_idx = 5,
};

static struct clk_regmap_gate clk_en_rtc = {
	.clkr.hw.init = CLK_HW_INIT("clk_en_rtc", "misc", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x10,
	.bit_idx = 9,
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

static struct clk_regmap_gate clk_en_i2c1 = {
	.clkr.hw.init = CLK_HW_INIT("clk_en_i2c1", "misc", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x10,
	.bit_idx = 16,
};

static struct clk_regmap_gate clk_en_ur1 = {
	.clkr.hw.init = CLK_HW_INIT("clk_en_ur1", "misc", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x10,
	.bit_idx = 28,
};

static const char * const ve_parents[] = {
	"clk_sys",
	"clk_sysh",
	"clk_ve"
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

static const char * const ve2_bpu_parents[] = {
	"clk_sys",
	"clk_sysh",
	"pll_gpu"
};

static struct clk_regmap_mux clk_ve2_bpu = {
	.clkr.hw.init = CLK_HW_INIT_PARENTS("clk_ve2_bpu", ve2_bpu_parents, &clk_regmap_mux_ops, CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT),
	.mux_ofs = 0x4c,
	.shift = 6,
	.mask = 0x3,
};

static struct clk_regmap *rtd1195_cc_regmap_clks[] = {
	&pll_scpu.clkp.clkr,
	&pll_bus.clkp.clkr,
	&pll_dcsb.clkr,
	&pll_gpu.clkr,
	&pll_vcpu.clkr,
	&clk_en_misc.clkr,
	&clk_en_hdmirx.clkr,
	&clk_en_gspi.clkr,
	&clk_en_usb.clkr,
	&clk_en_hdmi.clkr,
	&clk_en_etn.clkr,
	&clk_en_gpu.clkr,
	&clk_en_ve1.clkr,
	&clk_en_jpeg.clkr,
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
	&clk_en_ve2.clkr,
	&clk_en_sdio.clkr,
	&clk_en_sd_ip.clkr,
	&clk_en_i2c5.clkr,
	&clk_en_ve.clkr,
	&clk_en_rtc.clkr,
	&clk_en_i2c4.clkr,
	&clk_en_i2c3.clkr,
	&clk_en_i2c2.clkr,
	&clk_en_i2c1.clkr,
	&clk_en_ur1.clkr,
	&clk_ve1.clkr,
	&clk_ve2.clkr,
	&clk_ve2_bpu.clkr,
};

static struct clk_hw_onecell_data rtd1195_cc_hw_data = {
	.num = RTD1195_CRT_CLK_MAX,
	.hws = {
		[RTD1195_CRT_PLL_SCPU] = &__clk_pll_div_hw(&pll_scpu),
		[RTD1195_CRT_PLL_BUS] = &__clk_pll_div_hw(&pll_bus),
		[RTD1195_CRT_CLK_SYS] = &clk_sys.hw,
		[RTD1195_CRT_PLL_DCSB] = &__clk_pll_hw(&pll_dcsb),
		[RTD1195_CRT_CLK_SYSH] = &clk_sysh.hw,
		[RTD1195_CRT_PLL_GPU] = &__clk_pll_hw(&pll_gpu),
		[RTD1195_CRT_PLL_VCPU] = &__clk_pll_hw(&pll_vcpu),
		[RTD1195_CRT_CLK_VE1] = &__clk_regmap_mux_hw(&clk_ve1),
		[RTD1195_CRT_CLK_VE2] = &__clk_regmap_mux_hw(&clk_ve2),
		[RTD1195_CRT_CLK_VE2_BPU] = &__clk_regmap_mux_hw(&clk_ve2_bpu),
		[RTD1195_CRT_CLK_EN_MISC] = &__clk_regmap_gate_hw(&clk_en_misc),
		[RTD1195_CRT_CLK_EN_HDMIRX] = &__clk_regmap_gate_hw(&clk_en_hdmirx),
		[RTD1195_CRT_CLK_EN_GSPI] = &__clk_regmap_gate_hw(&clk_en_gspi),
		[RTD1195_CRT_CLK_EN_USB] = &__clk_regmap_gate_hw(&clk_en_usb),
		[RTD1195_CRT_CLK_EN_HDMI] = &__clk_regmap_gate_hw(&clk_en_hdmi),
		[RTD1195_CRT_CLK_EN_ETN] = &__clk_regmap_gate_hw(&clk_en_etn),
		[RTD1195_CRT_CLK_EN_GPU] = &__clk_regmap_gate_hw(&clk_en_gpu),
		[RTD1195_CRT_CLK_EN_VE1] = &__clk_regmap_gate_hw(&clk_en_ve1),
		[RTD1195_CRT_CLK_EN_VE2] = &__clk_regmap_gate_hw(&clk_en_ve2),
		[RTD1195_CRT_CLK_EN_VE_JPEG] = &__clk_regmap_gate_hw(&clk_en_jpeg),
		[RTD1195_CRT_CLK_EN_SE] = &__clk_regmap_gate_hw(&clk_en_se),
		[RTD1195_CRT_CLK_EN_CP] = &__clk_regmap_gate_hw(&clk_en_cp),
		[RTD1195_CRT_CLK_EN_MD] = &__clk_regmap_gate_hw(&clk_en_md),
		[RTD1195_CRT_CLK_EN_TP] = &__clk_regmap_gate_hw(&clk_en_tp),
		[RTD1195_CRT_CLK_EN_NF] = &__clk_regmap_gate_hw(&clk_en_nf),
		[RTD1195_CRT_CLK_EN_EMMC] = &__clk_regmap_gate_hw(&clk_en_emmc),
		[RTD1195_CRT_CLK_EN_CR] = &__clk_regmap_gate_hw(&clk_en_cr),
		[RTD1195_CRT_CLK_EN_SDIO_IP] = &__clk_regmap_gate_hw(&clk_en_sdio_ip),
		[RTD1195_CRT_CLK_EN_MIPI] = &__clk_regmap_gate_hw(&clk_en_mipi),
		[RTD1195_CRT_CLK_EN_EMMC_IP] = &__clk_regmap_gate_hw(&clk_en_emmc_ip),
		[RTD1195_CRT_CLK_EN_SDIO] = &__clk_regmap_gate_hw(&clk_en_sdio),
		[RTD1195_CRT_CLK_EN_SD_IP] = &__clk_regmap_gate_hw(&clk_en_sd_ip),
		[RTD1195_CRT_CLK_EN_MISC_I2C_5] = &__clk_regmap_gate_hw(&clk_en_i2c5),
		[RTD1195_CRT_CLK_EN_VE] = &__clk_regmap_gate_hw(&clk_en_ve),
		[RTD1195_CRT_CLK_EN_MISC_RTC] = &__clk_regmap_gate_hw(&clk_en_rtc),
		[RTD1195_CRT_CLK_EN_MISC_I2C_4] = &__clk_regmap_gate_hw(&clk_en_i2c4),
		[RTD1195_CRT_CLK_EN_MISC_I2C_3] = &__clk_regmap_gate_hw(&clk_en_i2c3),
		[RTD1195_CRT_CLK_EN_MISC_I2C_2] = &__clk_regmap_gate_hw(&clk_en_i2c2),
		[RTD1195_CRT_CLK_EN_MISC_I2C_1] = &__clk_regmap_gate_hw(&clk_en_i2c1),
		[RTD1195_CRT_CLK_EN_UR1] = &__clk_regmap_gate_hw(&clk_en_ur1),
		[RTD1195_CRT_CLK_MAX] = NULL,
	},
};

static struct rtk_reset_bank rtd1195_cc_reset_banks[] = {
	{ .ofs = 0x00, },
	{ .ofs = 0x04, },
};

static const struct rtk_clk_desc rtd1195_cc_desc = {
	.clk_data = &rtd1195_cc_hw_data,
	.clks = rtd1195_cc_regmap_clks,
	.num_clks = ARRAY_SIZE(rtd1195_cc_regmap_clks),
	.reset_banks = rtd1195_cc_reset_banks,
	.num_reset_banks = ARRAY_SIZE(rtd1195_cc_reset_banks),
};

static int rtd1195_cc_probe(struct platform_device *pdev)
{
	return rtk_clk_probe(pdev, &rtd1195_cc_desc);
}

static const struct of_device_id rtd1195_cc_match[] = {
	{ .compatible = "realtek,rtd1195-crt-clk", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rtd1195_cc_match);

static struct platform_driver rtd1195_cc_driver = {
	.probe = rtd1195_cc_probe,
	.driver = {
		.name = "rtk-rtd1195-crt-clk",
		.of_match_table = of_match_ptr(rtd1195_cc_match),
	},
};

static int __init rtd1195_cc_init(void)
{
	return platform_driver_register(&rtd1195_cc_driver);
}
subsys_initcall(rtd1195_cc_init);
MODULE_DESCRIPTION("Reatek RTD1195 CRT Controller Driver");
MODULE_AUTHOR("Cheng-Yu Lee <cylee12@realtek.com>");
MODULE_LICENSE("GPL v2");
