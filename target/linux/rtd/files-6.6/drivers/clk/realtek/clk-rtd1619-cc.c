// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2019 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */

#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include "common.h"
#include <dt-bindings/clock/rtd1619-clk.h>
#include <dt-bindings/reset/rtd1619-reset.h>

#define FREQ_NF_MASK          (0x7FFFF)
#define FREQ_NF(_r, _n, _f)   { .rate = _r, .val = ((_n) << 11) | (_f), }
#define FREQ_MNO_MASK         (0x63FF0)
#define FREQ_MNO(_r, _m, _n, _o) \
	{ .rate = _r, .val = ((_m) << 4) | ((_n) << 12) | ((_o) << 17), }

static const struct freq_table bus_tbl[] = {
	FREQ_NF(513000000, 35,    0),
	FREQ_NF(400000000, 26, 1289),
	FREQ_TABLE_END
};

static struct clk_pll pll_bus = {
	.ssc_ofs   = 0x520,
	.pll_ofs   = CLK_OFS_INVALID,
	.pll_type  = CLK_PLL_TYPE_NF_SSC,
	.freq_tbl  = bus_tbl,
	.freq_mask = FREQ_NF_MASK,
	.clkr.hw.init = CLK_HW_INIT("pll_bus", "osc27m", &clk_pll_ro_ops, CLK_GET_RATE_NOCACHE),
};

static CLK_FIXED_FACTOR(clk_sys, "clk_sys", "pll_bus", 2, 1, 0);

static const struct freq_table dcsb_tbl[] = {
	FREQ_NF(526500000, 36, 0),
	FREQ_NF(553500000, 38, 0),
	FREQ_TABLE_END
};

static struct clk_pll pll_dcsb = {
	.ssc_ofs   = 0x540,
	.pll_ofs   = CLK_OFS_INVALID,
	.pll_type  = CLK_PLL_TYPE_NF_SSC,
	.freq_tbl  = dcsb_tbl,
	.freq_mask = FREQ_NF_MASK,
	.clkr.hw.init = CLK_HW_INIT("pll_dcsb", "osc27m", &clk_pll_ro_ops, CLK_GET_RATE_NOCACHE),
};

static CLK_FIXED_FACTOR(clk_sysh, "clk_sysh", "pll_dcsb", 1, 1, 0);

static const struct freq_table ddsx_tbl[] = {
	FREQ_NF(432000000, 13, 0),
	FREQ_TABLE_END
};

static struct clk_pll pll_ddsa = {
	.ssc_ofs   = 0x560,
	.pll_ofs   = 0x120,
	.pow_loc   = CLK_PLL_CONF_POW_LOC_CTL3,
	.pll_type  = CLK_PLL_TYPE_NF_SSC,
	.freq_tbl  = ddsx_tbl,
	.freq_mask = FREQ_NF_MASK,
	.clkr.hw.init = CLK_HW_INIT("pll_ddsa", "osc27m", &clk_pll_ops, CLK_IGNORE_UNUSED | CLK_GET_RATE_NOCACHE),
};

static const struct freq_table gpu_tbl[] = {
	FREQ_NF(300000000, 19,  455),
	FREQ_NF(400000000, 26, 1289),
	FREQ_NF(500000000, 34,   75),
	FREQ_NF(600000000, 41,  910),
	FREQ_NF(650000000, 45,  303),
	FREQ_NF(700000000, 48, 1745),
	FREQ_NF(750000000, 52, 1137),
	FREQ_NF(800000000, 56,  530),
	FREQ_NF(850000000, 59, 1971),
	FREQ_TABLE_END
};

static struct clk_pll pll_gpu = {
	.ssc_ofs   = 0x5A0,
	.pll_ofs   = 0x1C0,
	.pow_loc   = CLK_PLL_CONF_POW_LOC_CTL2,
	.pll_type  = CLK_PLL_TYPE_NF_SSC,
	.freq_tbl  = gpu_tbl,
	.freq_mask = FREQ_NF_MASK,
	.clkr.hw.init = CLK_HW_INIT("pll_gpu", "osc27m", &clk_pll_ops, CLK_GET_RATE_NOCACHE),
};

static const struct freq_table ve_tbl[] = {
	FREQ_MNO(189000000, 12, 0, 1),
	FREQ_MNO(270000000, 18, 0, 1),
	FREQ_MNO(405000000, 13, 0, 0),
	FREQ_MNO(432000000, 14, 0, 0),
	FREQ_MNO(459000000, 15, 0, 0),
	FREQ_MNO(486000000, 16, 0, 0),
	FREQ_MNO(513000000, 17, 0, 0),
	FREQ_MNO(540000000, 18, 0, 0),
	FREQ_MNO(550000000, 59, 2, 0),
	FREQ_MNO(567000000, 19, 0, 0),
	FREQ_MNO(594000000, 20, 0, 0),
	FREQ_MNO(648000000, 22, 0, 0),
	FREQ_MNO(675000000, 23, 0, 0),
	FREQ_MNO(702000000, 24, 0, 0),
	FREQ_MNO(715000000, 51, 1, 0),
	FREQ_TABLE_END
};

static struct clk_pll pll_ve1 = {
	.ssc_ofs   = CLK_OFS_INVALID,
	.pll_ofs   = 0x114,
	.pow_loc   = CLK_PLL_CONF_POW_LOC_CTL2,
	.pll_type  = CLK_PLL_TYPE_MNO,
	.freq_tbl  = ve_tbl,
	.freq_mask = FREQ_MNO_MASK,
	.clkr.hw.init = CLK_HW_INIT("pll_ve1", "osc27m", &clk_pll_ops, CLK_GET_RATE_NOCACHE | CLK_SET_RATE_GATE),
};

static struct clk_pll pll_ve2 = {
	.ssc_ofs   = CLK_OFS_INVALID,
	.pll_ofs   = 0x1D0,
	.pow_loc   = CLK_PLL_CONF_POW_LOC_CTL2,
	.pll_type  = CLK_PLL_TYPE_MNO,
	.freq_tbl  = ve_tbl,
	.freq_mask = FREQ_MNO_MASK,
	.clkr.hw.init = CLK_HW_INIT("pll_ve2", "osc27m", &clk_pll_ops, CLK_GET_RATE_NOCACHE | CLK_SET_RATE_GATE),
};

static struct clk_pll_dif pll_dif = {
	.ssc_ofs   = 0x634,
	.pll_ofs   = 0x624,
	.clkr.hw.init = CLK_HW_INIT("pll_dif", "osc27m", &clk_pll_dif_ops, CLK_GET_RATE_NOCACHE),
	.adtv_conf = {
		0x00000048, 0x00020c00, 0x204004ca, 0x8000a000,
		0x00420c00, 0x00420c03, 0x00000078, 0x204084ca,
	},
};

static struct clk_pll_psaud pll_psaud1a = {
	.reg = 0x130,
	.id  = CLK_PLL_PSAUD1A,
	.clkr.hw.init = CLK_HW_INIT("pll_psaud1a", "osc27m", &clk_pll_psaud_ops, CLK_IGNORE_UNUSED | CLK_SET_RATE_UNGATE),
};

static struct clk_pll_psaud pll_psaud2a = {
	.reg = 0x130,
	.id  = CLK_PLL_PSAUD2A,
	.clkr.hw.init = CLK_HW_INIT("pll_psaud2a", "osc27m", &clk_pll_psaud_ops, CLK_IGNORE_UNUSED | CLK_SET_RATE_UNGATE),
};

static struct clk_regmap_clkdet clk_det_pll_bus = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("ref_pll_bus", &clk_regmap_clkdet_ops, CLK_GET_RATE_NOCACHE),
	.type = CLK_DET_TYPE_CRT,
	.ofs  = 0x424,
};

static struct clk_regmap_clkdet clk_det_pll_dcsb = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("ref_pll_dcsb", &clk_regmap_clkdet_ops, CLK_GET_RATE_NOCACHE),
	.type = CLK_DET_TYPE_CRT,
	.ofs  = 0x428,
};

static struct clk_regmap_clkdet clk_det_pll_acpu = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("ref_pll_acpu", &clk_regmap_clkdet_ops, CLK_GET_RATE_NOCACHE),
	.type = CLK_DET_TYPE_CRT,
	.ofs  = 0x42c,
};

static struct clk_regmap_clkdet clk_det_pll_ddsa = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("ref_pll_ddsa", &clk_regmap_clkdet_ops, CLK_GET_RATE_NOCACHE),
	.type = CLK_DET_TYPE_CRT,
	.ofs  = 0x430,
};

static struct clk_regmap_clkdet clk_det_pll_gpu = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("ref_pll_gpu", &clk_regmap_clkdet_ops, CLK_GET_RATE_NOCACHE),
	.type = CLK_DET_TYPE_CRT,
	.ofs  = 0x438,
};

static struct clk_regmap_clkdet clk_det_pll_ve1 = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("ref_pll_ve1", &clk_regmap_clkdet_ops, CLK_GET_RATE_NOCACHE),
	.type = CLK_DET_TYPE_CRT,
	.ofs  = 0x43c,
};

static struct clk_regmap_clkdet clk_det_pll_ve2 = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("ref_pll_ve2", &clk_regmap_clkdet_ops, CLK_GET_RATE_NOCACHE),
	.type = CLK_DET_TYPE_CRT,
	.ofs  = 0x440,
};

static struct clk_regmap_gate clk_en_misc = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_misc", &clk_regmap_gate_ops, CLK_IS_CRITICAL),
	.gate_ofs = 0x50,
	.bit_idx = 0,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_pcie0 = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_pcie0", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x50,
	.bit_idx = 2,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_gspi = {
	.clkr.hw.init = CLK_HW_INIT("clk_en_gspi", "misc", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x50,
	.bit_idx = 6,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_sds = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_sds", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x50,
	.bit_idx = 12,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_hdmi = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_hdmi", &clk_regmap_gate_ops, CLK_IGNORE_UNUSED),
	.gate_ofs = 0x50,
	.bit_idx = 14,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_gpu = {
	.clkr.hw.init = CLK_HW_INIT("clk_en_gpu", "pll_gpu", &clk_regmap_gate_ops, CLK_SET_RATE_PARENT),
	.gate_ofs = 0x50,
	.bit_idx = 18,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_ve1 = {
	.clkr.hw.init = CLK_HW_INIT("clk_en_ve1", "clk_ve1", &clk_regmap_gate_ops, CLK_SET_RATE_PARENT),
	.gate_ofs = 0x50,
	.bit_idx = 20,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_ve2 = {
	.clkr.hw.init = CLK_HW_INIT("clk_en_ve2", "clk_ve2", &clk_regmap_gate_ops, CLK_SET_RATE_PARENT),
	.gate_ofs = 0x50,
	.bit_idx = 22,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_tve = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_tve", &clk_regmap_gate_ops, CLK_IGNORE_UNUSED),
	.gate_ofs = 0x50,
	.bit_idx = 24,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_vo = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_vo", &clk_regmap_gate_ops, CLK_IGNORE_UNUSED),
	.gate_ofs = 0x50,
	.bit_idx = 26,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_lsadc = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_lsadc", &clk_regmap_gate_ops, CLK_IGNORE_UNUSED),
	.gate_ofs = 0x50,
	.bit_idx = 28,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_se = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_se", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x50,
	.bit_idx = 30,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_cp = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_cp", &clk_regmap_gate_ops, CLK_IGNORE_UNUSED),
	.gate_ofs = 0x54,
	.bit_idx = 2,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_md = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_md", &clk_regmap_gate_ops, CLK_IGNORE_UNUSED),
	.gate_ofs = 0x54,
	.bit_idx = 4,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_tp = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_tp", &clk_regmap_gate_ops, CLK_IGNORE_UNUSED),
	.gate_ofs = 0x54,
	.bit_idx = 6,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_rsa = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_rsa", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x54,
	.bit_idx = 8,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_nf = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_nf", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x54,
	.bit_idx = 10,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_emmc = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_emmc", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x54,
	.bit_idx = 12,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_sd = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_sd", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x54,
	.bit_idx = 14,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_sdio_ip = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_sdio_ip", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x54,
	.bit_idx = 16,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_mipi = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_mipi", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x54,
	.bit_idx = 18,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_emmc_ip = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_emmc_ip", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x54,
	.bit_idx = 20,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_sdio = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_sdio", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x54,
	.bit_idx = 22,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_sd_ip = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_sd_ip", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x54,
	.bit_idx = 24,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_cablerx = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_cablerx", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x54,
	.bit_idx = 26,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_tpb = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_tpb", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x54,
	.bit_idx = 28,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_sc1 = {
	.clkr.hw.init = CLK_HW_INIT("clk_en_sc1", "misc", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x54,
	.bit_idx = 30,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_i2c3 = {
	.clkr.hw.init = CLK_HW_INIT("clk_en_i2c3", "misc", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x58,
	.bit_idx = 0,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_jpeg = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_jpeg", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x58,
	.bit_idx = 4,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_sc0 = {
	.clkr.hw.init = CLK_HW_INIT("clk_en_sc0", "misc", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x58,
	.bit_idx = 10,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_hdmirx = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_hdmirx", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x58,
	.bit_idx = 26,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_hse = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_hse", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x58,
	.bit_idx = 28,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_ur2 = {
	.clkr.hw.init = CLK_HW_INIT("clk_en_ur2", "misc", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x58,
	.bit_idx = 30,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_ur1 = {
	.clkr.hw.init = CLK_HW_INIT("clk_en_ur1", "misc", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x5c,
	.bit_idx = 0,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_fan = {
	.clkr.hw.init = CLK_HW_INIT("clk_en_fan", "misc", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x5c,
	.bit_idx = 2,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_sata_wrap_sys = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_sata_wrap_sys", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x5c,
	.bit_idx = 8,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_sata_wrap_sysh = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_sata_wrap_sysh", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x5c,
	.bit_idx = 10,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_sata_mac_sysh = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_sata_mac_sysh", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x5c,
	.bit_idx = 12,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_r2rdsc = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_r2rdsc", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x5c,
	.bit_idx = 14,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_pcie1 = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_pcie1", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x5c,
	.bit_idx = 18,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_i2c4 = {
	.clkr.hw.init = CLK_HW_INIT("clk_en_i2c4", "misc", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x5c,
	.bit_idx = 20,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_i2c5 = {
	.clkr.hw.init = CLK_HW_INIT("clk_en_i2c5", "misc", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x5c,
	.bit_idx = 22,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_ve3 = {
	.clkr.hw.init = CLK_HW_INIT("clk_en_ve3", "clk_ve3", &clk_regmap_gate_ops, CLK_SET_RATE_PARENT),
	.gate_ofs = 0x5c,
	.bit_idx = 26,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_edp = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_edp", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x5c,
	.bit_idx = 28,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_tsio_trx = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_tsio_trx", &clk_regmap_gate_ops, CLK_IGNORE_UNUSED),
	.gate_ofs = 0x5c,
	.bit_idx = 30,
	.write_en = 1,
};

static const char * const ve_parents[] = {
	"clk_sys",
	"clk_sysh",
	"pll_ve1",
	"pll_ve2",
};

static struct clk_regmap_mux clk_ve1 = {
	.clkr.hw.init = CLK_HW_INIT_PARENTS("clk_ve1", ve_parents, &clk_regmap_mux_ops,
					    CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT),
	.mux_ofs = 0x4c,
	.shift = 0,
	.mask = 0x7,
};

static struct clk_regmap_mux clk_ve2 = {
	.clkr.hw.init = CLK_HW_INIT_PARENTS("clk_ve2", ve_parents, &clk_regmap_mux_ops,
					    CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT),
	.mux_ofs = 0x4c,
	.shift = 3,
	.mask = 0x7,
};

static struct clk_regmap_mux clk_ve3 = {
	.clkr.hw.init = CLK_HW_INIT_PARENTS("clk_ve3", ve_parents, &clk_regmap_mux_ops,
					    CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT),
	.mux_ofs = 0x4c,
	.shift = 6,
	.mask = 0x7,
};

static struct clk_regmap_mux clk_ve2_bpu = {
	.clkr.hw.init = CLK_HW_INIT_PARENTS("clk_ve2_bpu", ve_parents, &clk_regmap_mux_ops,
					    CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT),
	.mux_ofs = 0x4c,
	.shift = 9,
	.mask = 0x7,
};

static struct rtk_reset_bank rtd1619_cc_reset_banks[] = {
	{ .ofs = 0x00, .write_en = 1, },
	{ .ofs = 0x04, .write_en = 1, },
	{ .ofs = 0x08, .write_en = 1, },
	{ .ofs = 0x0c, .write_en = 1, },
	{ .ofs = 0x68, .write_en = 1, },
};

static struct clk_regmap *rtd1619_cc_regmap_clks[] = {
	&pll_bus.clkr,
	&pll_dcsb.clkr,
	&pll_ddsa.clkr,
	&pll_gpu.clkr,
	&pll_ve1.clkr,
	&pll_ve2.clkr,
	&pll_dif.clkr,
	&pll_psaud1a.clkr,
	&pll_psaud2a.clkr,
	&clk_det_pll_bus.clkr,
	&clk_det_pll_dcsb.clkr,
	&clk_det_pll_acpu.clkr,
	&clk_det_pll_ddsa.clkr,
	&clk_det_pll_gpu.clkr,
	&clk_det_pll_ve1.clkr,
	&clk_det_pll_ve2.clkr,
	&clk_en_misc.clkr,
	&clk_en_pcie0.clkr,
	&clk_en_gspi.clkr,
	&clk_en_sds.clkr,
	&clk_en_hdmi.clkr,
	&clk_en_gpu.clkr,
	&clk_en_ve1.clkr,
	&clk_en_ve2.clkr,
	&clk_en_tve.clkr,
	&clk_en_vo.clkr,
	&clk_en_lsadc.clkr,
	&clk_en_se.clkr,
	&clk_en_cp.clkr,
	&clk_en_md.clkr,
	&clk_en_tp.clkr,
	&clk_en_rsa.clkr,
	&clk_en_nf.clkr,
	&clk_en_emmc.clkr,
	&clk_en_sd.clkr,
	&clk_en_sdio_ip.clkr,
	&clk_en_mipi.clkr,
	&clk_en_emmc_ip.clkr,
	&clk_en_sdio.clkr,
	&clk_en_sd_ip.clkr,
	&clk_en_cablerx.clkr,
	&clk_en_tpb.clkr,
	&clk_en_sc1.clkr,
	&clk_en_i2c3.clkr,
	&clk_en_jpeg.clkr,
	&clk_en_sc0.clkr,
	&clk_en_hdmirx.clkr,
	&clk_en_hse.clkr,
	&clk_en_ur2.clkr,
	&clk_en_ur1.clkr,
	&clk_en_fan.clkr,
	&clk_en_sata_wrap_sys.clkr,
	&clk_en_sata_wrap_sysh.clkr,
	&clk_en_sata_mac_sysh.clkr,
	&clk_en_r2rdsc.clkr,
	&clk_en_pcie1.clkr,
	&clk_en_i2c4.clkr,
	&clk_en_i2c5.clkr,
	&clk_en_ve3.clkr,
	&clk_en_edp.clkr,
	&clk_en_tsio_trx.clkr,
	&clk_ve1.clkr,
	&clk_ve2.clkr,
	&clk_ve3.clkr,
	&clk_ve2_bpu.clkr,
};

static struct clk_hw_onecell_data rtd1619_cc_hw_data = {
	.num = RTD1619_CRT_CLK_MAX,
	.hws = {
		[RTD1619_CRT_PLL_BUS] = &__clk_pll_hw(&pll_bus),
		[RTD1619_CRT_CLK_SYS] = &clk_sys.hw,
		[RTD1619_CRT_PLL_DCSB] = &__clk_pll_hw(&pll_dcsb),
		[RTD1619_CRT_CLK_SYSH] = &clk_sysh.hw,
		[RTD1619_CRT_PLL_DDSA] = &__clk_pll_hw(&pll_ddsa),
		[RTD1619_CRT_PLL_GPU] = &__clk_pll_hw(&pll_gpu),
		[RTD1619_CRT_PLL_VE1] = &__clk_pll_hw(&pll_ve1),
		[RTD1619_CRT_PLL_VE2] = &__clk_pll_hw(&pll_ve2),
		[RTD1619_CRT_CLK_VE1] = &__clk_regmap_mux_hw(&clk_ve1),
		[RTD1619_CRT_CLK_VE2] = &__clk_regmap_mux_hw(&clk_ve2),
		[RTD1619_CRT_CLK_VE3] = &__clk_regmap_mux_hw(&clk_ve3),
		[RTD1619_CRT_CLK_VE2_BPU] = &__clk_regmap_mux_hw(&clk_ve2_bpu),
		[RTD1619_CRT_PLL_DIF] = &__clk_pll_dif_hw(&pll_dif),
		[RTD1619_CRT_PLL_PSAUD1A] = &__clk_pll_psaud_hw(&pll_psaud1a),
		[RTD1619_CRT_PLL_PSAUD2A] = &__clk_pll_psaud_hw(&pll_psaud2a),
		[RTD1619_CRT_CLK_EN_MISC] = &__clk_regmap_gate_hw(&clk_en_misc),
		[RTD1619_CRT_CLK_EN_PCIE0] = &__clk_regmap_gate_hw(&clk_en_pcie0),
		[RTD1619_CRT_CLK_EN_GSPI] = &__clk_regmap_gate_hw(&clk_en_gspi),
		[RTD1619_CRT_CLK_EN_SDS] = &__clk_regmap_gate_hw(&clk_en_sds),
		[RTD1619_CRT_CLK_EN_HDMI] = &__clk_regmap_gate_hw(&clk_en_hdmi),
		[RTD1619_CRT_CLK_EN_GPU] = &__clk_regmap_gate_hw(&clk_en_gpu),
		[RTD1619_CRT_CLK_EN_VE1] = &__clk_regmap_gate_hw(&clk_en_ve1),
		[RTD1619_CRT_CLK_EN_VE2] = &__clk_regmap_gate_hw(&clk_en_ve2),
		[RTD1619_CRT_CLK_EN_TVE] = &__clk_regmap_gate_hw(&clk_en_tve),
		[RTD1619_CRT_CLK_EN_VO] = &__clk_regmap_gate_hw(&clk_en_vo),
		[RTD1619_CRT_CLK_EN_LSADC] = &__clk_regmap_gate_hw(&clk_en_lsadc),
		[RTD1619_CRT_CLK_EN_SE] = &__clk_regmap_gate_hw(&clk_en_se),
		[RTD1619_CRT_CLK_EN_CP] = &__clk_regmap_gate_hw(&clk_en_cp),
		[RTD1619_CRT_CLK_EN_MD] = &__clk_regmap_gate_hw(&clk_en_md),
		[RTD1619_CRT_CLK_EN_TP] = &__clk_regmap_gate_hw(&clk_en_tp),
		[RTD1619_CRT_CLK_EN_RSA] = &__clk_regmap_gate_hw(&clk_en_rsa),
		[RTD1619_CRT_CLK_EN_NF] = &__clk_regmap_gate_hw(&clk_en_nf),
		[RTD1619_CRT_CLK_EN_EMMC] = &__clk_regmap_gate_hw(&clk_en_emmc),
		[RTD1619_CRT_CLK_EN_SD] = &__clk_regmap_gate_hw(&clk_en_sd),
		[RTD1619_CRT_CLK_EN_SDIO_IP] = &__clk_regmap_gate_hw(&clk_en_sdio_ip),
		[RTD1619_CRT_CLK_EN_MIPI] = &__clk_regmap_gate_hw(&clk_en_mipi),
		[RTD1619_CRT_CLK_EN_EMMC_IP] = &__clk_regmap_gate_hw(&clk_en_emmc_ip),
		[RTD1619_CRT_CLK_EN_SDIO] = &__clk_regmap_gate_hw(&clk_en_sdio),
		[RTD1619_CRT_CLK_EN_SD_IP] = &__clk_regmap_gate_hw(&clk_en_sd_ip),
		[RTD1619_CRT_CLK_EN_CABLERX] = &__clk_regmap_gate_hw(&clk_en_cablerx),
		[RTD1619_CRT_CLK_EN_TPB] = &__clk_regmap_gate_hw(&clk_en_tpb),
		[RTD1619_CRT_CLK_EN_MISC_SC1] = &__clk_regmap_gate_hw(&clk_en_sc1),
		[RTD1619_CRT_CLK_EN_MISC_I2C_3] = &__clk_regmap_gate_hw(&clk_en_i2c3),
		[RTD1619_CRT_CLK_EN_JPEG] = &__clk_regmap_gate_hw(&clk_en_jpeg),
		[RTD1619_CRT_CLK_EN_MISC_SC0] = &__clk_regmap_gate_hw(&clk_en_sc0),
		[RTD1619_CRT_CLK_EN_HDMIRX] = &__clk_regmap_gate_hw(&clk_en_hdmirx),
		[RTD1619_CRT_CLK_EN_HSE] = &__clk_regmap_gate_hw(&clk_en_hse),
		[RTD1619_CRT_CLK_EN_UR2] = &__clk_regmap_gate_hw(&clk_en_ur2),
		[RTD1619_CRT_CLK_EN_UR1] = &__clk_regmap_gate_hw(&clk_en_ur1),
		[RTD1619_CRT_CLK_EN_FAN] = &__clk_regmap_gate_hw(&clk_en_fan),
		[RTD1619_CRT_CLK_EN_SATA_WRAP_SYS] = &__clk_regmap_gate_hw(&clk_en_sata_wrap_sys),
		[RTD1619_CRT_CLK_EN_SATA_WRAP_SYSH] = &__clk_regmap_gate_hw(&clk_en_sata_wrap_sysh),
		[RTD1619_CRT_CLK_EN_SATA_MAC_SYSH] = &__clk_regmap_gate_hw(&clk_en_sata_mac_sysh),
		[RTD1619_CRT_CLK_EN_R2RDSC] = &__clk_regmap_gate_hw(&clk_en_r2rdsc),
		[RTD1619_CRT_CLK_EN_PCIE1] = &__clk_regmap_gate_hw(&clk_en_pcie1),
		[RTD1619_CRT_CLK_EN_MISC_I2C_4] = &__clk_regmap_gate_hw(&clk_en_i2c4),
		[RTD1619_CRT_CLK_EN_MISC_I2C_5] = &__clk_regmap_gate_hw(&clk_en_i2c5),
		[RTD1619_CRT_CLK_EN_VE3] = &__clk_regmap_gate_hw(&clk_en_ve3),
		[RTD1619_CRT_CLK_EN_EDP] = &__clk_regmap_gate_hw(&clk_en_edp),
		[RTD1619_CRT_CLK_EN_TSIO_TRX] = &__clk_regmap_gate_hw(&clk_en_tsio_trx),
		[RTD1619_CRT_CLK_DET_PLL_BUS] = &__clk_regmap_clkdet_hw(&clk_det_pll_bus),
		[RTD1619_CRT_CLK_DET_PLL_DCSB] = &__clk_regmap_clkdet_hw(&clk_det_pll_dcsb),
		[RTD1619_CRT_CLK_DET_PLL_ACPU] = &__clk_regmap_clkdet_hw(&clk_det_pll_acpu),
		[RTD1619_CRT_CLK_DET_PLL_DDSA] = &__clk_regmap_clkdet_hw(&clk_det_pll_ddsa),
		[RTD1619_CRT_CLK_DET_PLL_GPU] = &__clk_regmap_clkdet_hw(&clk_det_pll_gpu),
		[RTD1619_CRT_CLK_DET_PLL_VE1] = &__clk_regmap_clkdet_hw(&clk_det_pll_ve1),
		[RTD1619_CRT_CLK_DET_PLL_VE2] = &__clk_regmap_clkdet_hw(&clk_det_pll_ve2),
		[RTD1619_CRT_CLK_MAX] = NULL,
	},
};

static const struct rtk_clk_desc rtd1619_cc_desc = {
	.clk_data = &rtd1619_cc_hw_data,
	.clks = rtd1619_cc_regmap_clks,
	.num_clks = ARRAY_SIZE(rtd1619_cc_regmap_clks),
	.reset_banks = rtd1619_cc_reset_banks,
	.num_reset_banks = ARRAY_SIZE(rtd1619_cc_reset_banks),
};

static int rtd1619_cc_probe(struct platform_device *pdev)
{
	return rtk_clk_probe(pdev, &rtd1619_cc_desc);
}

static const struct of_device_id rtd1619_cc_match[] = {
	{ .compatible = "realtek,rtd1619-crt-clk", },
	{ .compatible = "realtek,rtd1611-crt-clk", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rtd1619_cc_match);

static struct platform_driver rtd1619_cc_driver = {
	.probe = rtd1619_cc_probe,
	.driver = {
		.name = "rtk-rtd1619-crt-clk",
		.of_match_table = of_match_ptr(rtd1619_cc_match),
	},
};

static int __init rtd1619_cc_init(void)
{
	return platform_driver_register(&rtd1619_cc_driver);
}
subsys_initcall(rtd1619_cc_init);
MODULE_DESCRIPTION("Reatek RTD1619 CRT Controller Driver");
MODULE_AUTHOR("Cheng-Yu Lee <cylee12@realtek.com>");
MODULE_LICENSE("GPL v2");
