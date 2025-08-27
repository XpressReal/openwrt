// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */

#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <dt-bindings/clock/rtd1319d-clk.h>
#include <dt-bindings/reset/rtd1319d-reset.h>
#include "common.h"

#define FREQ_NF_MASK          (0x7FFFF)
#define FREQ_NF(_r, _nf)   { .rate = _r, .val = (_nf), }

static const struct freq_table bus_tbl[] = {
	FREQ_NF(499500000, 0x11000),
	FREQ_TABLE_END
};

static const struct freq_table dcsb_tbl[] = {
	FREQ_NF(472500000, 0x10000),
	FREQ_TABLE_END
};

static const struct freq_table gpu_tbl[] = {
	FREQ_NF(300000000, 0x099c7),
	FREQ_NF(400000000, 0x0d509),
	FREQ_NF(500000000, 0x1104b),
	FREQ_NF(600000000, 0x14b8e),
	FREQ_NF(634500000, 0x16000),
	FREQ_NF(661500000, 0x17000),
	FREQ_NF(769500000, 0x1b000),
	FREQ_TABLE_END
};

static const struct freq_table ve_tbl[] = {
	FREQ_NF(526500000, 0x12000),
	FREQ_NF(553500000, 0x13000),
	FREQ_NF(661500000, 0x17000),
	FREQ_NF(756000000, 0x1a800),
	FREQ_TABLE_END
};

static const struct freq_table hifi_tbl[] = {
	FREQ_NF(486000000, 0x10800),
	FREQ_NF(904500000, 0x20000),
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

static struct clk_pll pll_dcsb = {
	.ssc_ofs   = 0x540,
	.pll_ofs   = CLK_OFS_INVALID,
	.pll_type  = CLK_PLL_TYPE_NF_SSC,
	.freq_tbl  = dcsb_tbl,
	.freq_mask = FREQ_NF_MASK,
	.clkr.hw.init = CLK_HW_INIT("pll_dcsb", "osc27m", &clk_pll_ro_ops, CLK_GET_RATE_NOCACHE),
};

static CLK_FIXED_FACTOR(clk_sys, "clk_sys", "pll_bus", 2, 1, 0);
static CLK_FIXED_FACTOR(clk_sysh, "clk_sysh", "pll_dcsb", 1, 1, 0);

static const struct freq_table ddsx_tbl[] = {
	FREQ_NF(432000000, 0x0e800),
	FREQ_TABLE_END
};

static struct clk_pll pll_ddsa = {
	.ssc_ofs   = 0x560,
	.pll_ofs   = 0x120,
	.pow_loc   = CLK_PLL_CONF_POW_LOC_CTL3,
	.pll_type  = CLK_PLL_TYPE_NF_SSC,
	.freq_tbl  = ddsx_tbl,
	.freq_mask = FREQ_NF_MASK,
	.clkr.hw.init = CLK_HW_INIT("pll_ddsa", "osc27m", &clk_pll_ops,
		CLK_IS_CRITICAL | CLK_GET_RATE_NOCACHE),
};

static struct clk_pll pll_gpu = {
	.ssc_ofs   = 0x5a0,
	.pll_ofs   = 0x1C0,
	.pow_loc   = CLK_PLL_CONF_POW_LOC_CTL2,
	.pll_type  = CLK_PLL_TYPE_NF_SSC,
	.freq_tbl  = gpu_tbl,
	.freq_mask = FREQ_NF_MASK,
	.clkr.hw.init = CLK_HW_INIT("pll_gpu", "osc27m", &clk_pll_ops, CLK_GET_RATE_NOCACHE),
	.pow_set_rs = 1,
	.rs_mask = 0x0003c000,
	.rs_val = 0x00014000,
	.pow_set_pi_bps = 1,
};

static struct clk_pll pll_ve1 = {
	.pll_ofs   = 0x114,
	.ssc_ofs   = 0x580,
	.pow_loc   = CLK_PLL_CONF_POW_LOC_CTL2,
	.pll_type  = CLK_PLL_TYPE_NF_SSC,
	.freq_tbl  = ve_tbl,
	.freq_mask = FREQ_NF_MASK,
	.clkr.hw.init = CLK_HW_INIT("pll_ve1", "osc27m", &clk_pll_ops, CLK_GET_RATE_NOCACHE),
	.pow_set_rs = 1,
	.rs_mask = 0x0003c000,
	.rs_val = 0x00014000,
	.pow_set_pi_bps = 1,
};

static struct clk_pll pll_ve2 = {
	.ssc_ofs   = 0x5e0,
	.pll_ofs   = 0x1d0,
	.pow_loc   = CLK_PLL_CONF_POW_LOC_CTL2,
	.pll_type  = CLK_PLL_TYPE_NF_SSC,
	.freq_tbl  = ve_tbl,
	.freq_mask = FREQ_NF_MASK,
	.clkr.hw.init = CLK_HW_INIT("pll_ve2", "osc27m", &clk_pll_ops, CLK_GET_RATE_NOCACHE | CLK_IGNORE_UNUSED),
	.pow_set_rs = 1,
	.rs_mask = 0x0003c000,
	.rs_val = 0x00014000,
	.pow_set_pi_bps = 1,
};

static struct clk_pll_dif pll_dif = {
	.ssc_ofs   = 0x634,
	.pll_ofs   = 0x624,
	.clkr.hw.init = CLK_HW_INIT("pll_dif", "osc27m", &clk_pll_dif_v2_ops, CLK_GET_RATE_NOCACHE),
};

static struct clk_pll_psaud pll_psaud1a = {
	.reg = 0x130,
	.id  = CLK_PLL_PSAUD1A,
	.clkr.hw.init = CLK_HW_INIT("pll_psaud1a", "osc27m", &clk_pll_psaud_ops,
		CLK_IGNORE_UNUSED | CLK_SET_RATE_UNGATE),
};

static struct clk_pll_psaud pll_psaud2a = {
	.reg = 0x130,
	.id  = CLK_PLL_PSAUD2A,
	.clkr.hw.init = CLK_HW_INIT("pll_psaud2a", "osc27m", &clk_pll_psaud_ops,
		CLK_IGNORE_UNUSED | CLK_SET_RATE_UNGATE),
};

static struct clk_pll pll_hifi = {
	.ssc_ofs   = 0x6e0,
	.pll_ofs   = 0x1d8,
	.pll_type  = CLK_PLL_TYPE_NF_SSC,
	.pow_loc   = CLK_PLL_CONF_POW_LOC_CTL2,
	.freq_tbl  = hifi_tbl,
	.freq_mask = FREQ_NF_MASK,
	.clkr.hw.init = CLK_HW_INIT("pll_hifi", "osc27m", &clk_pll_ops, CLK_GET_RATE_NOCACHE),
	.pow_set_rs = 1,
	.rs_mask = 0x0003c000,
	.rs_val = 0x00014000,
	.pow_set_pi_bps = 1,
};

static const char * const clk_sc_parents[] = { "osc27m", "clk216m" };

static struct clk_regmap_mux clk_sc0 = {
	.mux_ofs = 0x038,
	.shift = 28,
	.mask = 0x1,
	.clkr.hw.init = CLK_HW_INIT_PARENTS("clk_sc0",  clk_sc_parents, &clk_regmap_mux_ops, 0),
};

static struct clk_regmap_mux clk_sc1 = {
	.mux_ofs = 0x038,
	.shift = 29,
	.mask = 0x1,
	.clkr.hw.init = CLK_HW_INIT_PARENTS("clk_sc1",  clk_sc_parents, &clk_regmap_mux_ops, 0),
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

static struct clk_regmap_clkdet clk_det_pll_hifi = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("ref_pll_hifi", &clk_regmap_clkdet_ops, CLK_GET_RATE_NOCACHE),
	.type = CLK_DET_TYPE_CRT,
	.ofs  = 0x448,
};

static struct clk_fixed_factor pll_emmc_ref = {
	.div = 6,
	.mult = 1,
	.hw.init = CLK_HW_INIT("pll_emmc_ref", "osc27m", &clk_fixed_factor_ops, 0),
};

static struct clk_pll_mmc pll_emmc = {
	.pll_ofs = 0x1f0,
	.clkr.hw.init = CLK_HW_INIT("pll_emmc", "pll_emmc_ref", &clk_pll_mmc_ops, 0),
	.phase0_hw.init = CLK_HW_INIT("pll_emmc_vp0", "pll_emmc", &clk_pll_mmc_phase_ops, 0),
	.phase1_hw.init = CLK_HW_INIT("pll_emmc_vp1", "pll_emmc", &clk_pll_mmc_phase_ops, 0),
};

static const struct freq_table rtd1315e_bus_tbl[] = {
	FREQ_NF(337500000, 0x0a800),
	FREQ_NF(459000000, 0x0f000),
	FREQ_TABLE_END
};

static const struct freq_table rtd1315e_ddsa_tbl[] = {
	FREQ_NF(432000000, 0x0e000),
	FREQ_TABLE_END
};

static const struct freq_table rtd1315e_gpu_tbl[] = {
	FREQ_NF(300000000, 0x091c7),
	FREQ_NF(400000000, 0x0cd09),
	FREQ_NF(500000000, 0x1084b),
	FREQ_NF(580500000, 0x13800),
	FREQ_NF(610000000, 0x1497b),
	FREQ_NF(634500000, 0x15800),
	FREQ_NF(661500000, 0x16800),
	FREQ_TABLE_END
};

static const struct freq_table rtd1315e_ve_tbl[] = {
	FREQ_NF(351000000, 0x0b000),
	FREQ_NF(459000000, 0x0f000),
	FREQ_NF(526500000, 0x11800),
	FREQ_NF(553500000, 0x12800),
	FREQ_NF(661500000, 0x16800),
	FREQ_NF(756000000, 0x1a000),
	FREQ_TABLE_END
};

static const struct freq_table rtd1315e_hifi_tbl[] = {
	FREQ_NF(904500000, 0x1f800),
	FREQ_TABLE_END
};

static struct clk_pll rtd1315e_pll_bus = {
	.ssc_ofs   = 0x520,
	.pll_ofs   = CLK_OFS_INVALID,
	.pll_type  = CLK_PLL_TYPE_NF_SSC,
	.freq_tbl  = rtd1315e_bus_tbl,
	.freq_mask = FREQ_NF_MASK,
	.clkr.hw.init = CLK_HW_INIT("pll_bus", "osc27m", &clk_pll_ro_ops, CLK_GET_RATE_NOCACHE),
};

static struct clk_pll rtd1315e_pll_dcsb = {
	.ssc_ofs   = 0x540,
	.pll_ofs   = CLK_OFS_INVALID,
	.pll_type  = CLK_PLL_TYPE_NF_SSC,
	.freq_tbl  = rtd1315e_bus_tbl,
	.freq_mask = FREQ_NF_MASK,
	.clkr.hw.init = CLK_HW_INIT("pll_dcsb", "osc27m", &clk_pll_ro_ops, CLK_GET_RATE_NOCACHE),
};

static struct clk_pll rtd1315e_pll_ddsa = {
	.ssc_ofs   = 0x560,
	.pll_ofs   = 0x120,
	.pow_loc   = CLK_PLL_CONF_POW_LOC_CTL3,
	.pll_type  = CLK_PLL_TYPE_NF_SSC,
	.freq_tbl  = rtd1315e_ddsa_tbl,
	.freq_mask = FREQ_NF_MASK,
	.clkr.hw.init = CLK_HW_INIT("pll_ddsa", "osc27m", &clk_pll_ops,
		CLK_IS_CRITICAL | CLK_GET_RATE_NOCACHE),
};

static struct clk_pll rtd1315e_pll_gpu = {
	.ssc_ofs   = 0x5a0,
	.pll_ofs   = 0x1C0,
	.pow_loc   = CLK_PLL_CONF_POW_LOC_CTL2,
	.pll_type  = CLK_PLL_TYPE_NF_SSC,
	.freq_tbl  = rtd1315e_gpu_tbl,
	.freq_mask = FREQ_NF_MASK,
	.clkr.hw.init = CLK_HW_INIT("pll_gpu", "osc27m", &clk_pll_ops, CLK_GET_RATE_NOCACHE),
	.pow_set_rs = 1,
	.rs_mask = 0x0003c000,
	.rs_val = 0x00014000,
	.pow_set_pi_bps = 1,
};

static struct clk_pll rtd1315e_pll_ve1 = {
	.pll_ofs   = 0x114,
	.ssc_ofs   = 0x580,
	.pow_loc   = CLK_PLL_CONF_POW_LOC_CTL2,
	.pll_type  = CLK_PLL_TYPE_NF_SSC,
	.freq_tbl  = rtd1315e_ve_tbl,
	.freq_mask = FREQ_NF_MASK,
	.clkr.hw.init = CLK_HW_INIT("pll_ve1", "osc27m", &clk_pll_ops, CLK_GET_RATE_NOCACHE),
	.pow_set_rs = 1,
	.rs_mask = 0x0003c000,
	.rs_val = 0x00014000,
	.pow_set_pi_bps = 1,
};

static struct clk_pll rtd1315e_pll_ve2 = {
	.ssc_ofs   = 0x5e0,
	.pll_ofs   = 0x1d0,
	.pow_loc   = CLK_PLL_CONF_POW_LOC_CTL2,
	.pll_type  = CLK_PLL_TYPE_NF_SSC,
	.freq_tbl  = rtd1315e_ve_tbl,
	.freq_mask = FREQ_NF_MASK,
	.clkr.hw.init = CLK_HW_INIT("pll_ve2", "osc27m", &clk_pll_ops, CLK_GET_RATE_NOCACHE | CLK_IGNORE_UNUSED),
	.pow_set_rs = 1,
	.rs_mask = 0x0003c000,
	.rs_val = 0x00014000,
	.pow_set_pi_bps = 1,
};

static struct clk_pll rtd1315e_pll_hifi = {
	.ssc_ofs   = 0x6e0,
	.pll_ofs   = 0x1d8,
	.pll_type  = CLK_PLL_TYPE_NF_SSC,
	.pow_loc   = CLK_PLL_CONF_POW_LOC_CTL2,
	.freq_tbl  = rtd1315e_hifi_tbl,
	.freq_mask = FREQ_NF_MASK,
	.clkr.hw.init = CLK_HW_INIT("pll_hifi", "osc27m", &clk_pll_ops, CLK_GET_RATE_NOCACHE),
	.pow_set_rs = 1,
	.rs_mask = 0x0003c000,
	.rs_val = 0x00014000,
	.pow_set_pi_bps = 1,
};

static struct clk_fixed_factor rtd1315e_pll_emmc_ref = {
	.div = 1,
	.mult = 1,
	.hw.init = CLK_HW_INIT("pll_emmc_ref", "osc27m", &clk_fixed_factor_ops, 0),
};

static struct clk_pll_mmc rtd1315e_pll_emmc = {
	.pll_ofs = 0x1f0,
	.ssc_dig_ofs = 0x6b0,
	.clkr.hw.init = CLK_HW_INIT("pll_emmc", "pll_emmc_ref", &clk_pll_mmc_v2_ops, 0),
	.phase0_hw.init = CLK_HW_INIT("pll_emmc_vp0", "pll_emmc", &clk_pll_mmc_phase_ops, 0),
	.phase1_hw.init = CLK_HW_INIT("pll_emmc_vp1", "pll_emmc", &clk_pll_mmc_phase_ops, 0),
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
	.clkr.hw.init = CLK_HW_INIT("clk_en_gspi", "clk_en_misc", &clk_regmap_gate_ops, 0),
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
	.clkr.hw.init = CLK_HW_INIT("clk_en_ve2", "clk_ve2", &clk_regmap_gate_ops, CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED),
	.gate_ofs = 0x50,
	.bit_idx = 22,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_cp = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_cp", &clk_regmap_gate_ops, CLK_IGNORE_UNUSED),
	.gate_ofs = 0x54,
	.bit_idx = 2,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_tp = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_tp", &clk_regmap_gate_ops, CLK_IS_CRITICAL),
	.gate_ofs = 0x54,
	.bit_idx = 6,
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
	.clkr.hw.init = CLK_HW_INIT("clk_en_emmc_ip", "pll_emmc", &clk_regmap_gate_ops, CLK_SET_RATE_PARENT),
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

static struct clk_regmap_gate clk_en_misc_sc1 = {
	.clkr.hw.init = CLK_HW_INIT("clk_en_misc_sc1", "clk_en_misc", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x54,
	.bit_idx = 30,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_misc_i2c_3 = {
	.clkr.hw.init = CLK_HW_INIT("clk_en_misc_i2c_3", "clk_en_misc", &clk_regmap_gate_ops, 0),
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

static struct clk_regmap_gate clk_en_misc_sc0 = {
	.clkr.hw.init = CLK_HW_INIT("clk_en_misc_sc0", "clk_en_misc", &clk_regmap_gate_ops, 0),
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
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_hse", &clk_regmap_gate_ops, CLK_IS_CRITICAL),
	.gate_ofs = 0x58,
	.bit_idx = 28,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_ur2 = {
	.clkr.hw.init = CLK_HW_INIT("clk_en_ur2", "clk_en_misc", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x58,
	.bit_idx = 30,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_ur1 = {
	.clkr.hw.init = CLK_HW_INIT("clk_en_ur1", "clk_en_misc", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x5c,
	.bit_idx = 0,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_fan = {
	.clkr.hw.init = CLK_HW_INIT("clk_en_fan", "clk_en_misc", &clk_regmap_gate_ops, 0),
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

static struct clk_regmap_gate clk_en_tpc = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_tpc", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x5c,
	.bit_idx = 16,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_pcie1 = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_pcie1", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x5c,
	.bit_idx = 18,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_misc_i2c_4 = {
	.clkr.hw.init = CLK_HW_INIT("clk_en_misc_i2c_4", "clk_en_misc", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x5c,
	.bit_idx = 20,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_misc_i2c_5 = {
	.clkr.hw.init = CLK_HW_INIT("clk_en_misc_i2c_5", "clk_en_misc", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x5c,
	.bit_idx = 22,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_tsio = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_tsio", &clk_regmap_gate_ops, CLK_IGNORE_UNUSED),
	.gate_ofs = 0x5c,
	.bit_idx = 24,
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

static struct clk_regmap_gate clk_en_pcie2 = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_pcie2", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x8c,
	.bit_idx = 0,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_lite = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_lite", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x8c,
	.bit_idx = 6,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_mipi_dsi = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_mipi_dsi", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x8c,
	.bit_idx = 8,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_aucpu0 = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_aucpu0", &clk_regmap_gate_ro_ops, 0),
	.gate_ofs = 0x8c,
	.bit_idx = 14,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_cablerx_q = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_cablerx_q", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x8c,
	.bit_idx = 16,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_hdmitop = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_hdmitop", &clk_regmap_gate_ops, CLK_IGNORE_UNUSED),
	.gate_ofs = 0x8c,
	.bit_idx = 20,
	.write_en = 1,
};

static struct clk_regmap_gate clk_en_mdlm2m = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_mdlm2m", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0xb0,
	.bit_idx = 6,
	.write_en = 1,
};

static const char * const clk_hifi_parents[] = { "pll_hifi", "pll_hifi", "pll_gpu", "pll_dcsb" };

static struct clk_regmap_mux clk_hifi = {
	.clkr.hw.init = CLK_HW_INIT_PARENTS("clk_hifi", clk_hifi_parents, &clk_regmap_mux_ops,
					    CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT),
	.mux_ofs = 0x28,
	.shift = 0,
	.mask = 0x3,
};

static const char * const clk_hifi_iso_parents[] = { "pll_hifi", "osc27m", "clk_sys", "clk_sys" };

static struct clk_regmap_mux clk_hifi_iso = {
	.clkr.hw.init = CLK_HW_INIT_PARENTS("clk_hifi_iso", clk_hifi_iso_parents, &clk_regmap_mux_ops,
					    CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT),
	.mux_ofs = 0x28,
	.shift = 8,
	.mask = 0x3,
};

static const char * const clk_ve_parents[] = { "pll_vodma", "clk_sysh", "pll_ve1", "pll_ve2" };

static struct clk_regmap_mux clk_ve1 = {
	.clkr.hw.init = CLK_HW_INIT_PARENTS("clk_ve1", clk_ve_parents, &clk_regmap_mux_ops,
					    CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT),
	.mux_ofs = 0x4c,
	.shift = 0,
	.mask = 0x7,
};

static struct clk_regmap_mux clk_ve2 = {
	.clkr.hw.init = CLK_HW_INIT_PARENTS("clk_ve2", clk_ve_parents, &clk_regmap_mux_ops,
					    CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT | CLK_IGNORE_UNUSED),
	.mux_ofs = 0x4c,
	.shift = 3,
	.mask = 0x7,
};

static struct clk_regmap_mux clk_ve3 = {
	.clkr.hw.init = CLK_HW_INIT_PARENTS("clk_ve3", clk_ve_parents, &clk_regmap_mux_ops,
					    CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT),
	.mux_ofs = 0x4c,
	.shift = 6,
	.mask = 0x7,
};

static struct clk_regmap_mux clk_ve3_bpu = {
	.clkr.hw.init = CLK_HW_INIT_PARENTS("clk_ve3_bpu", clk_ve_parents, &clk_regmap_mux_ops,
					    CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT),
	.mux_ofs = 0x4c,
	.shift = 9,
	.mask = 0x7,
};

static struct rtk_reset_bank rtd1319d_cc_reset_banks[] = {
	{ .ofs = 0x000, .write_en = 1, },
	{ .ofs = 0x004, .write_en = 1, },
	{ .ofs = 0x008, .write_en = 1, },
	{ .ofs = 0x00c, .write_en = 1, },
	{ .ofs = 0x068, .write_en = 1, },
	{ .ofs = 0x090, .write_en = 1, },
	{ .ofs = 0x454, },
	{ .ofs = 0x458, },
	{ .ofs = 0x464, },
	{ .ofs = 0x0b8, .write_en = 1, }
};

static struct clk_regmap *rtd1319d_cc_regmap_clks[] = {
	&pll_bus.clkr,
	&pll_dcsb.clkr,
	&pll_ddsa.clkr,
	&pll_gpu.clkr,
	&pll_ve1.clkr,
	&pll_ve2.clkr,
	&pll_dif.clkr,
	&pll_psaud1a.clkr,
	&pll_psaud2a.clkr,
	&pll_hifi.clkr,
	&clk_sc0.clkr,
	&clk_sc1.clkr,
	&clk_det_pll_bus.clkr,
	&clk_det_pll_dcsb.clkr,
	&clk_det_pll_acpu.clkr,
	&clk_det_pll_ddsa.clkr,
	&clk_det_pll_gpu.clkr,
	&clk_det_pll_ve1.clkr,
	&clk_det_pll_ve2.clkr,
	&clk_det_pll_hifi.clkr,
	&pll_emmc.clkr,
	&rtd1315e_pll_bus.clkr,
	&rtd1315e_pll_dcsb.clkr,
	&rtd1315e_pll_ddsa.clkr,
	&rtd1315e_pll_gpu.clkr,
	&rtd1315e_pll_ve1.clkr,
	&rtd1315e_pll_ve2.clkr,
	&rtd1315e_pll_hifi.clkr,
	&rtd1315e_pll_emmc.clkr,
	&clk_en_misc.clkr,
	&clk_en_pcie0.clkr,
	&clk_en_gspi.clkr,
	&clk_en_sds.clkr,
	&clk_en_hdmi.clkr,
	&clk_en_gpu.clkr,
	&clk_en_ve1.clkr,
	&clk_en_ve2.clkr,
	&clk_en_cp.clkr,
	&clk_en_tp.clkr,
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
	&clk_en_misc_sc1.clkr,
	&clk_en_misc_i2c_3.clkr,
	&clk_en_jpeg.clkr,
	&clk_en_misc_sc0.clkr,
	&clk_en_hdmirx.clkr,
	&clk_en_hse.clkr,
	&clk_en_ur2.clkr,
	&clk_en_ur1.clkr,
	&clk_en_fan.clkr,
	&clk_en_sata_wrap_sys.clkr,
	&clk_en_sata_wrap_sysh.clkr,
	&clk_en_sata_mac_sysh.clkr,
	&clk_en_r2rdsc.clkr,
	&clk_en_tpc.clkr,
	&clk_en_pcie1.clkr,
	&clk_en_misc_i2c_4.clkr,
	&clk_en_misc_i2c_5.clkr,
	&clk_en_tsio.clkr,
	&clk_en_ve3.clkr,
	&clk_en_edp.clkr,
	&clk_en_tsio_trx.clkr,
	&clk_en_pcie2.clkr,
	&clk_en_lite.clkr,
	&clk_en_mipi_dsi.clkr,
	&clk_en_aucpu0.clkr,
	&clk_en_cablerx_q.clkr,
	&clk_en_hdmitop.clkr,
	&clk_en_mdlm2m.clkr,
	&clk_hifi.clkr,
	&clk_hifi_iso.clkr,
	&clk_ve1.clkr,
	&clk_ve2.clkr,
	&clk_ve3.clkr,
	&clk_ve3_bpu.clkr,
};

static struct clk_hw_onecell_data rtd1319d_cc_hw_data = {
	.num = RTD1319D_CRT_CLK_MAX,
	.hws = {
		[RTD1319D_CRT_CLK_EN_MISC] = &__clk_regmap_gate_hw(&clk_en_misc),
		[RTD1319D_CRT_CLK_EN_PCIE0] = &__clk_regmap_gate_hw(&clk_en_pcie0),
		[RTD1319D_CRT_CLK_EN_GSPI] = &__clk_regmap_gate_hw(&clk_en_gspi),
		[RTD1319D_CRT_CLK_EN_SDS] = &__clk_regmap_gate_hw(&clk_en_sds),
		[RTD1319D_CRT_CLK_EN_HDMI] = &__clk_regmap_gate_hw(&clk_en_hdmi),
		[RTD1319D_CRT_CLK_EN_GPU] = &__clk_regmap_gate_hw(&clk_en_gpu),
		[RTD1319D_CRT_CLK_EN_VE1] = &__clk_regmap_gate_hw(&clk_en_ve1),
		[RTD1319D_CRT_CLK_EN_VE2] = &__clk_regmap_gate_hw(&clk_en_ve2),
		[RTD1319D_CRT_CLK_EN_CP] = &__clk_regmap_gate_hw(&clk_en_cp),
		[RTD1319D_CRT_CLK_EN_TP] = &__clk_regmap_gate_hw(&clk_en_tp),
		[RTD1319D_CRT_CLK_EN_NF] = &__clk_regmap_gate_hw(&clk_en_nf),
		[RTD1319D_CRT_CLK_EN_EMMC] = &__clk_regmap_gate_hw(&clk_en_emmc),
		[RTD1319D_CRT_CLK_EN_SD] = &__clk_regmap_gate_hw(&clk_en_sd),
		[RTD1319D_CRT_CLK_EN_SDIO_IP] = &__clk_regmap_gate_hw(&clk_en_sdio_ip),
		[RTD1319D_CRT_CLK_EN_MIPI] = &__clk_regmap_gate_hw(&clk_en_mipi),
		[RTD1319D_CRT_CLK_EN_EMMC_IP] = &__clk_regmap_gate_hw(&clk_en_emmc_ip),
		[RTD1319D_CRT_CLK_EN_SDIO] = &__clk_regmap_gate_hw(&clk_en_sdio),
		[RTD1319D_CRT_CLK_EN_SD_IP] = &__clk_regmap_gate_hw(&clk_en_sd_ip),
		[RTD1319D_CRT_CLK_EN_CABLERX] = &__clk_regmap_gate_hw(&clk_en_cablerx),
		[RTD1319D_CRT_CLK_EN_TPB] = &__clk_regmap_gate_hw(&clk_en_tpb),
		[RTD1319D_CRT_CLK_EN_MISC_SC1] = &__clk_regmap_gate_hw(&clk_en_misc_sc1),
		[RTD1319D_CRT_CLK_EN_MISC_I2C_3] = &__clk_regmap_gate_hw(&clk_en_misc_i2c_3),
		[RTD1319D_CRT_CLK_EN_JPEG] = &__clk_regmap_gate_hw(&clk_en_jpeg),
		[RTD1319D_CRT_CLK_EN_MISC_SC0] = &__clk_regmap_gate_hw(&clk_en_misc_sc0),
		[RTD1319D_CRT_CLK_EN_HDMIRX] = &__clk_regmap_gate_hw(&clk_en_hdmirx),
		[RTD1319D_CRT_CLK_EN_HSE] = &__clk_regmap_gate_hw(&clk_en_hse),
		[RTD1319D_CRT_CLK_EN_UR2] = &__clk_regmap_gate_hw(&clk_en_ur2),
		[RTD1319D_CRT_CLK_EN_UR1] = &__clk_regmap_gate_hw(&clk_en_ur1),
		[RTD1319D_CRT_CLK_EN_FAN] = &__clk_regmap_gate_hw(&clk_en_fan),
		[RTD1319D_CRT_CLK_EN_SATA_WRAP_SYS] = &__clk_regmap_gate_hw(&clk_en_sata_wrap_sys),
		[RTD1319D_CRT_CLK_EN_SATA_WRAP_SYSH] = &__clk_regmap_gate_hw(&clk_en_sata_wrap_sysh),
		[RTD1319D_CRT_CLK_EN_SATA_MAC_SYSH] = &__clk_regmap_gate_hw(&clk_en_sata_mac_sysh),
		[RTD1319D_CRT_CLK_EN_R2RDSC] = &__clk_regmap_gate_hw(&clk_en_r2rdsc),
		[RTD1319D_CRT_CLK_EN_TPC] = &__clk_regmap_gate_hw(&clk_en_tpc),
		[RTD1319D_CRT_CLK_EN_PCIE1] = &__clk_regmap_gate_hw(&clk_en_pcie1),
		[RTD1319D_CRT_CLK_EN_MISC_I2C_4] = &__clk_regmap_gate_hw(&clk_en_misc_i2c_4),
		[RTD1319D_CRT_CLK_EN_MISC_I2C_5] = &__clk_regmap_gate_hw(&clk_en_misc_i2c_5),
		[RTD1319D_CRT_CLK_EN_TSIO] = &__clk_regmap_gate_hw(&clk_en_tsio),
		[RTD1319D_CRT_CLK_EN_VE3] = &__clk_regmap_gate_hw(&clk_en_ve3),
		[RTD1319D_CRT_CLK_EN_EDP] = &__clk_regmap_gate_hw(&clk_en_edp),
		[RTD1319D_CRT_CLK_EN_TSIO_TRX] = &__clk_regmap_gate_hw(&clk_en_tsio_trx),
		[RTD1319D_CRT_CLK_EN_PCIE2] = &__clk_regmap_gate_hw(&clk_en_pcie2),
		[RTD1319D_CRT_CLK_EN_LITE] = &__clk_regmap_gate_hw(&clk_en_lite),
		[RTD1319D_CRT_CLK_EN_MIPI_DSI] = &__clk_regmap_gate_hw(&clk_en_mipi_dsi),
		[RTD1319D_CRT_CLK_EN_AUCPU0] = &__clk_regmap_gate_hw(&clk_en_aucpu0),
		[RTD1319D_CRT_CLK_EN_CABLERX_Q] = &__clk_regmap_gate_hw(&clk_en_cablerx_q),
		[RTD1319D_CRT_CLK_EN_HDMITOP] = &__clk_regmap_gate_hw(&clk_en_hdmitop),
		[RTD1319D_CRT_CLK_EN_MDLM2M] = &__clk_regmap_gate_hw(&clk_en_mdlm2m),
		[RTD1319D_CRT_PLL_BUS] = &__clk_pll_hw(&pll_bus),
		[RTD1319D_CRT_PLL_DCSB] = &__clk_pll_hw(&pll_dcsb),
		[RTD1319D_CRT_CLK_SYS] = &clk_sys.hw,
		[RTD1319D_CRT_CLK_SYSH] = &clk_sysh.hw,
		[RTD1319D_CRT_PLL_DDSA] = &__clk_pll_hw(&pll_ddsa),
		[RTD1319D_CRT_PLL_GPU] = &__clk_pll_hw(&pll_gpu),
		[RTD1319D_CRT_PLL_VE1] = &__clk_pll_hw(&pll_ve1),
		[RTD1319D_CRT_PLL_VE2] = &__clk_pll_hw(&pll_ve2),
		[RTD1319D_CRT_CLK_VE1] = &__clk_regmap_mux_hw(&clk_ve1),
		[RTD1319D_CRT_CLK_VE2] = &__clk_regmap_mux_hw(&clk_ve2),
		[RTD1319D_CRT_CLK_VE3] = &__clk_regmap_mux_hw(&clk_ve3),
		[RTD1319D_CRT_CLK_VE3_BPU] = &__clk_regmap_mux_hw(&clk_ve3_bpu),
		[RTD1319D_CRT_PLL_DIF] = &__clk_pll_dif_hw(&pll_dif),
		[RTD1319D_CRT_PLL_PSAUD1A] = &__clk_pll_psaud_hw(&pll_psaud1a),
		[RTD1319D_CRT_PLL_PSAUD2A] = &__clk_pll_psaud_hw(&pll_psaud2a),
		[RTD1319D_CRT_PLL_HIFI] = &__clk_pll_hw(&pll_hifi),
		[RTD1319D_CRT_CLK_HIFI] = &__clk_regmap_mux_hw(&clk_hifi),
		[RTD1319D_CRT_CLK_HIFI_ISO] = &__clk_regmap_mux_hw(&clk_hifi_iso),
		[RTD1319D_CRT_PLL_EMMC_REF] = &pll_emmc_ref.hw,
		[RTD1319D_CRT_PLL_EMMC] = &__clk_pll_mmc_hw(&pll_emmc),
		[RTD1319D_CRT_PLL_EMMC_VP0] = &pll_emmc.phase0_hw,
		[RTD1319D_CRT_PLL_EMMC_VP1] = &pll_emmc.phase1_hw,
		[RTD1319D_CRT_CLK_SC0] = &__clk_regmap_mux_hw(&clk_sc0),
		[RTD1319D_CRT_CLK_SC1] = &__clk_regmap_mux_hw(&clk_sc1),
		[RTD1319D_CRT_CLK_DET_PLL_BUS] = &__clk_regmap_clkdet_hw(&clk_det_pll_bus),
		[RTD1319D_CRT_CLK_DET_PLL_DCSB] = &__clk_regmap_clkdet_hw(&clk_det_pll_dcsb),
		[RTD1319D_CRT_CLK_DET_PLL_ACPU] = &__clk_regmap_clkdet_hw(&clk_det_pll_acpu),
		[RTD1319D_CRT_CLK_DET_PLL_DDSA] = &__clk_regmap_clkdet_hw(&clk_det_pll_ddsa),
		[RTD1319D_CRT_CLK_DET_PLL_GPU] = &__clk_regmap_clkdet_hw(&clk_det_pll_gpu),
		[RTD1319D_CRT_CLK_DET_PLL_VE1] = &__clk_regmap_clkdet_hw(&clk_det_pll_ve1),
		[RTD1319D_CRT_CLK_DET_PLL_VE2] = &__clk_regmap_clkdet_hw(&clk_det_pll_ve2),
		[RTD1319D_CRT_CLK_DET_PLL_HIFI] = &__clk_regmap_clkdet_hw(&clk_det_pll_hifi),
		[RTD1319D_CRT_CLK_MAX] = NULL,
	},
};

static const struct rtk_clk_desc rtd1319d_cc_desc = {
	.clk_data = &rtd1319d_cc_hw_data,
	.clks = rtd1319d_cc_regmap_clks,
	.num_clks = ARRAY_SIZE(rtd1319d_cc_regmap_clks),
	.reset_banks = rtd1319d_cc_reset_banks,
	.num_reset_banks = ARRAY_SIZE(rtd1319d_cc_reset_banks),
};

static struct clk_hw_onecell_data rtd1315e_cc_hw_data = {
	.num = RTD1319D_CRT_CLK_MAX,
	.hws = {
		[RTD1319D_CRT_CLK_EN_MISC] = &__clk_regmap_gate_hw(&clk_en_misc),
		[RTD1319D_CRT_CLK_EN_PCIE0] = &__clk_regmap_gate_hw(&clk_en_pcie0),
		[RTD1319D_CRT_CLK_EN_GSPI] = &__clk_regmap_gate_hw(&clk_en_gspi),
		[RTD1319D_CRT_CLK_EN_SDS] = &__clk_regmap_gate_hw(&clk_en_sds),
		[RTD1319D_CRT_CLK_EN_HDMI] = &__clk_regmap_gate_hw(&clk_en_hdmi),
		[RTD1319D_CRT_CLK_EN_GPU] = &__clk_regmap_gate_hw(&clk_en_gpu),
		[RTD1319D_CRT_CLK_EN_VE1] = &__clk_regmap_gate_hw(&clk_en_ve1),
		[RTD1319D_CRT_CLK_EN_VE2] = &__clk_regmap_gate_hw(&clk_en_ve2),
		[RTD1319D_CRT_CLK_EN_CP] = &__clk_regmap_gate_hw(&clk_en_cp),
		[RTD1319D_CRT_CLK_EN_TP] = &__clk_regmap_gate_hw(&clk_en_tp),
		[RTD1319D_CRT_CLK_EN_NF] = &__clk_regmap_gate_hw(&clk_en_nf),
		[RTD1319D_CRT_CLK_EN_EMMC] = &__clk_regmap_gate_hw(&clk_en_emmc),
		[RTD1319D_CRT_CLK_EN_SD] = &__clk_regmap_gate_hw(&clk_en_sd),
		[RTD1319D_CRT_CLK_EN_SDIO_IP] = &__clk_regmap_gate_hw(&clk_en_sdio_ip),
		[RTD1319D_CRT_CLK_EN_MIPI] = &__clk_regmap_gate_hw(&clk_en_mipi),
		[RTD1319D_CRT_CLK_EN_EMMC_IP] = &__clk_regmap_gate_hw(&clk_en_emmc_ip),
		[RTD1319D_CRT_CLK_EN_SDIO] = &__clk_regmap_gate_hw(&clk_en_sdio),
		[RTD1319D_CRT_CLK_EN_SD_IP] = &__clk_regmap_gate_hw(&clk_en_sd_ip),
		[RTD1319D_CRT_CLK_EN_CABLERX] = &__clk_regmap_gate_hw(&clk_en_cablerx),
		[RTD1319D_CRT_CLK_EN_TPB] = &__clk_regmap_gate_hw(&clk_en_tpb),
		[RTD1319D_CRT_CLK_EN_MISC_SC1] = &__clk_regmap_gate_hw(&clk_en_misc_sc1),
		[RTD1319D_CRT_CLK_EN_MISC_I2C_3] = &__clk_regmap_gate_hw(&clk_en_misc_i2c_3),
		[RTD1319D_CRT_CLK_EN_JPEG] = &__clk_regmap_gate_hw(&clk_en_jpeg),
		[RTD1319D_CRT_CLK_EN_MISC_SC0] = &__clk_regmap_gate_hw(&clk_en_misc_sc0),
		[RTD1319D_CRT_CLK_EN_HDMIRX] = &__clk_regmap_gate_hw(&clk_en_hdmirx),
		[RTD1319D_CRT_CLK_EN_HSE] = &__clk_regmap_gate_hw(&clk_en_hse),
		[RTD1319D_CRT_CLK_EN_UR2] = &__clk_regmap_gate_hw(&clk_en_ur2),
		[RTD1319D_CRT_CLK_EN_UR1] = &__clk_regmap_gate_hw(&clk_en_ur1),
		[RTD1319D_CRT_CLK_EN_FAN] = &__clk_regmap_gate_hw(&clk_en_fan),
		[RTD1319D_CRT_CLK_EN_SATA_WRAP_SYS] = &__clk_regmap_gate_hw(&clk_en_sata_wrap_sys),
		[RTD1319D_CRT_CLK_EN_SATA_WRAP_SYSH] = &__clk_regmap_gate_hw(&clk_en_sata_wrap_sysh),
		[RTD1319D_CRT_CLK_EN_SATA_MAC_SYSH] = &__clk_regmap_gate_hw(&clk_en_sata_mac_sysh),
		[RTD1319D_CRT_CLK_EN_R2RDSC] = &__clk_regmap_gate_hw(&clk_en_r2rdsc),
		[RTD1319D_CRT_CLK_EN_TPC] = &__clk_regmap_gate_hw(&clk_en_tpc),
		[RTD1319D_CRT_CLK_EN_PCIE1] = &__clk_regmap_gate_hw(&clk_en_pcie1),
		[RTD1319D_CRT_CLK_EN_MISC_I2C_4] = &__clk_regmap_gate_hw(&clk_en_misc_i2c_4),
		[RTD1319D_CRT_CLK_EN_MISC_I2C_5] = &__clk_regmap_gate_hw(&clk_en_misc_i2c_5),
		[RTD1319D_CRT_CLK_EN_TSIO] = &__clk_regmap_gate_hw(&clk_en_tsio),
		[RTD1319D_CRT_CLK_EN_VE3] = &__clk_regmap_gate_hw(&clk_en_ve3),
		[RTD1319D_CRT_CLK_EN_EDP] = &__clk_regmap_gate_hw(&clk_en_edp),
		[RTD1319D_CRT_CLK_EN_TSIO_TRX] = &__clk_regmap_gate_hw(&clk_en_tsio_trx),
		[RTD1319D_CRT_CLK_EN_PCIE2] = &__clk_regmap_gate_hw(&clk_en_pcie2),
		[RTD1319D_CRT_CLK_EN_LITE] = &__clk_regmap_gate_hw(&clk_en_lite),
		[RTD1319D_CRT_CLK_EN_MIPI_DSI] = &__clk_regmap_gate_hw(&clk_en_mipi_dsi),
		[RTD1319D_CRT_CLK_EN_AUCPU0] = &__clk_regmap_gate_hw(&clk_en_aucpu0),
		[RTD1319D_CRT_CLK_EN_CABLERX_Q] = &__clk_regmap_gate_hw(&clk_en_cablerx_q),
		[RTD1319D_CRT_CLK_EN_HDMITOP] = &__clk_regmap_gate_hw(&clk_en_hdmitop),
		[RTD1319D_CRT_CLK_EN_MDLM2M] = &__clk_regmap_gate_hw(&clk_en_mdlm2m),
		[RTD1319D_CRT_PLL_BUS] = &__clk_pll_hw(&rtd1315e_pll_bus),
		[RTD1319D_CRT_PLL_DCSB] = &__clk_pll_hw(&rtd1315e_pll_dcsb),
		[RTD1319D_CRT_CLK_SYS] = &clk_sys.hw,
		[RTD1319D_CRT_CLK_SYSH] = &clk_sysh.hw,
		[RTD1319D_CRT_PLL_DDSA] = &__clk_pll_hw(&rtd1315e_pll_ddsa),
		[RTD1319D_CRT_PLL_GPU] = &__clk_pll_hw(&rtd1315e_pll_gpu),
		[RTD1319D_CRT_PLL_VE1] = &__clk_pll_hw(&rtd1315e_pll_ve1),
		[RTD1319D_CRT_PLL_VE2] = &__clk_pll_hw(&rtd1315e_pll_ve2),
		[RTD1319D_CRT_CLK_VE1] = &__clk_regmap_mux_hw(&clk_ve1),
		[RTD1319D_CRT_CLK_VE2] = &__clk_regmap_mux_hw(&clk_ve2),
		[RTD1319D_CRT_CLK_VE3] = &__clk_regmap_mux_hw(&clk_ve3),
		[RTD1319D_CRT_CLK_VE3_BPU] = &__clk_regmap_mux_hw(&clk_ve3_bpu),
		[RTD1319D_CRT_PLL_DIF] = &__clk_pll_dif_hw(&pll_dif),
		[RTD1319D_CRT_PLL_PSAUD1A] = &__clk_pll_psaud_hw(&pll_psaud1a),
		[RTD1319D_CRT_PLL_PSAUD2A] = &__clk_pll_psaud_hw(&pll_psaud2a),
		[RTD1319D_CRT_PLL_HIFI] = &__clk_pll_hw(&rtd1315e_pll_hifi),
		[RTD1319D_CRT_CLK_HIFI] = &__clk_regmap_mux_hw(&clk_hifi),
		[RTD1319D_CRT_CLK_HIFI_ISO] = &__clk_regmap_mux_hw(&clk_hifi_iso),
		[RTD1319D_CRT_PLL_EMMC_REF] = &rtd1315e_pll_emmc_ref.hw,
		[RTD1319D_CRT_PLL_EMMC] = &__clk_pll_mmc_hw(&rtd1315e_pll_emmc),
		[RTD1319D_CRT_PLL_EMMC_VP0] = &rtd1315e_pll_emmc.phase0_hw,
		[RTD1319D_CRT_PLL_EMMC_VP1] = &rtd1315e_pll_emmc.phase1_hw,
		[RTD1319D_CRT_CLK_SC0] = &__clk_regmap_mux_hw(&clk_sc0),
		[RTD1319D_CRT_CLK_SC1] = &__clk_regmap_mux_hw(&clk_sc1),
		[RTD1319D_CRT_CLK_DET_PLL_BUS] = &__clk_regmap_clkdet_hw(&clk_det_pll_bus),
		[RTD1319D_CRT_CLK_DET_PLL_DCSB] = &__clk_regmap_clkdet_hw(&clk_det_pll_dcsb),
		[RTD1319D_CRT_CLK_DET_PLL_ACPU] = &__clk_regmap_clkdet_hw(&clk_det_pll_acpu),
		[RTD1319D_CRT_CLK_DET_PLL_DDSA] = &__clk_regmap_clkdet_hw(&clk_det_pll_ddsa),
		[RTD1319D_CRT_CLK_DET_PLL_GPU] = &__clk_regmap_clkdet_hw(&clk_det_pll_gpu),
		[RTD1319D_CRT_CLK_DET_PLL_VE1] = &__clk_regmap_clkdet_hw(&clk_det_pll_ve1),
		[RTD1319D_CRT_CLK_DET_PLL_VE2] = &__clk_regmap_clkdet_hw(&clk_det_pll_ve2),
		[RTD1319D_CRT_CLK_DET_PLL_HIFI] = &__clk_regmap_clkdet_hw(&clk_det_pll_hifi),
		[RTD1319D_CRT_CLK_MAX] = NULL,
	},
};

static const struct rtk_clk_desc rtd1315e_cc_desc = {
	.clk_data = &rtd1315e_cc_hw_data,
	.clks = rtd1319d_cc_regmap_clks,
	.num_clks = ARRAY_SIZE(rtd1319d_cc_regmap_clks),
	.reset_banks = rtd1319d_cc_reset_banks,
	.num_reset_banks = ARRAY_SIZE(rtd1319d_cc_reset_banks),
};

static int rtd1319d_cc_probe(struct platform_device *pdev)
{
	const struct rtk_clk_desc *desc;

	desc = of_device_get_match_data(&pdev->dev);
	if (!desc)
		return -EINVAL;

	return rtk_clk_probe(pdev, desc);
}

static const struct of_device_id rtd1319d_cc_match[] = {
	{ .compatible = "realtek,rtd1319d-crt-clk", .data = &rtd1319d_cc_desc,},
	{ .compatible = "realtek,rtd1319d-crt-clk-n", .data = &rtd1319d_cc_desc,},
	{ .compatible = "realtek,rtd1315e-crt-clk", .data = &rtd1315e_cc_desc,},
	{ .compatible = "realtek,rtd1315e-crt-clk-n", .data = &rtd1315e_cc_desc,},
	{ /* sentinel */ }
};

static struct platform_driver rtd1319d_cc_driver = {
	.probe = rtd1319d_cc_probe,
	.driver = {
		.name = "rtk-rtd1319d-crt-clk",
		.of_match_table = rtd1319d_cc_match,
	},
};

static int __init rtd1319d_cc_init(void)
{
	return platform_driver_register(&rtd1319d_cc_driver);
}
subsys_initcall(rtd1319d_cc_init);

MODULE_DESCRIPTION("Reatek RTD1319D CRT Controller Driver");
MODULE_AUTHOR("Cheng-Yu Lee <cylee12@realtek.com>");
MODULE_LICENSE("GPL v2");
