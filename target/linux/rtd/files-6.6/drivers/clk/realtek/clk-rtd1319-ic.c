// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2019 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */

#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <dt-bindings/clock/rtd1319-clk.h>
#include "common.h"

static struct clk_regmap_gate clk_en_iso_gspi = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_iso_gspi", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x64,
	.bit_idx = 9,
};

static struct clk_regmap_gate clk_en_lsadc_ecoa2 = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_lsadc_ecoa2", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x8c,
	.bit_idx = 0,
};

static struct clk_regmap_gate clk_en_cec0 = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_cec0", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x8c,
	.bit_idx = 2,
};

static struct clk_regmap_gate clk_en_cbusrx_sys = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_cbusrx_sys", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x8c,
	.bit_idx = 3,
};

static struct clk_regmap_gate clk_en_cbustx_sys = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_cbustx_sys", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x8c,
	.bit_idx = 4,
};

static struct clk_regmap_gate clk_en_cbus_sys = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_cbus_sys", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x8c,
	.bit_idx = 5,
};

static struct clk_regmap_gate clk_en_cbus_osc = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_cbus_osc", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x8c,
	.bit_idx = 6,
};

static struct clk_regmap_gate clk_en_ir = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_ir", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x8c,
	.bit_idx = 7,
};

static struct clk_regmap_gate clk_en_i2c0 = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_i2c0", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x8c,
	.bit_idx = 9,
};

static struct clk_regmap_gate clk_en_i2c1 = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_i2c1", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x8c,
	.bit_idx = 10,
};

static struct clk_regmap_gate clk_en_etn_250m = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_etn_250m", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x8c,
	.bit_idx = 11,
};

static struct clk_regmap_gate clk_en_etn_sys = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_etn_sys", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x8c,
	.bit_idx = 12,
};

static struct clk_regmap_gate clk_en_usb_drd = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_usb_drd", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x8c,
	.bit_idx = 13,
};

static struct clk_regmap_gate clk_en_usb_host = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_usb_host", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x8c,
	.bit_idx = 14,
};

static struct clk_regmap_gate clk_en_usb_u3_host = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_usb_u3_host", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x8c,
	.bit_idx = 15,
};

static struct clk_regmap_gate clk_en_usb = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_usb", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x8c,
	.bit_idx = 16,
};


static struct rtk_reset_bank ic_reset_banks[] = {
	{ .ofs = 0x88, },
};

static struct clk_regmap *rtd1319_ic_regmap_clks[] = {
	&clk_en_iso_gspi.clkr,
	&clk_en_lsadc_ecoa2.clkr,
	&clk_en_cec0.clkr,
	&clk_en_cbusrx_sys.clkr,
	&clk_en_cbustx_sys.clkr,
	&clk_en_cbus_sys.clkr,
	&clk_en_cbus_osc.clkr,
	&clk_en_ir.clkr,
	&clk_en_i2c0.clkr,
	&clk_en_i2c1.clkr,
	&clk_en_etn_250m.clkr,
	&clk_en_etn_sys.clkr,
	&clk_en_usb_drd.clkr,
	&clk_en_usb_host.clkr,
	&clk_en_usb_u3_host.clkr,
	&clk_en_usb.clkr,
};

static struct clk_hw_onecell_data rtd1319_ic_hw_data = {
	.num = RTD1319_ISO_CLK_MAX,
	.hws = {
		[RTD1319_ISO_CLK_EN_LSADC_ECOA2] = &__clk_regmap_gate_hw(&clk_en_lsadc_ecoa2),
		[RTD1319_ISO_CLK_EN_CEC0] = &__clk_regmap_gate_hw(&clk_en_cec0),
		[RTD1319_ISO_CLK_EN_CBUSRX_SYS] = &__clk_regmap_gate_hw(&clk_en_cbusrx_sys),
		[RTD1319_ISO_CLK_EN_CBUSTX_SYS] = &__clk_regmap_gate_hw(&clk_en_cbustx_sys),
		[RTD1319_ISO_CLK_EN_CBUS_SYS] = &__clk_regmap_gate_hw(&clk_en_cbus_sys),
		[RTD1319_ISO_CLK_EN_CBUS_OSC] = &__clk_regmap_gate_hw(&clk_en_cbus_osc),
		[RTD1319_ISO_CLK_EN_IR] = &__clk_regmap_gate_hw(&clk_en_ir),
		[RTD1319_ISO_CLK_EN_I2C0] = &__clk_regmap_gate_hw(&clk_en_i2c0),
		[RTD1319_ISO_CLK_EN_I2C1] = &__clk_regmap_gate_hw(&clk_en_i2c1),
		[RTD1319_ISO_CLK_EN_ETN_250M] = &__clk_regmap_gate_hw(&clk_en_etn_250m),
		[RTD1319_ISO_CLK_EN_ETN_SYS] = &__clk_regmap_gate_hw(&clk_en_etn_sys),
		[RTD1319_ISO_CLK_EN_USB_DRD] = &__clk_regmap_gate_hw(&clk_en_usb_drd),
		[RTD1319_ISO_CLK_EN_USB_HOST] = &__clk_regmap_gate_hw(&clk_en_usb_host),
		[RTD1319_ISO_CLK_EN_USB_U3_HOST] = &__clk_regmap_gate_hw(&clk_en_usb_u3_host),
		[RTD1319_ISO_CLK_EN_USB] = &__clk_regmap_gate_hw(&clk_en_usb),
		[RTD1319_ISO_CLK_EN_ISO_GSPI] = &__clk_regmap_gate_hw(&clk_en_iso_gspi),
		[RTD1319_ISO_CLK_MAX] = NULL,
	},
};

static const struct rtk_clk_desc rtd1319_ic_desc = {
	.clk_data = &rtd1319_ic_hw_data,
	.clks = rtd1319_ic_regmap_clks,
	.num_clks = ARRAY_SIZE(rtd1319_ic_regmap_clks),
	.reset_banks = ic_reset_banks,
	.num_reset_banks = ARRAY_SIZE(ic_reset_banks),
};

static int rtd1319_ic_probe(struct platform_device *pdev)
{
	return rtk_clk_probe(pdev, &rtd1319_ic_desc);
}

static const struct of_device_id rtd1319_ic_match[] = {
	{ .compatible = "realtek,rtd1319-iso-clk", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rtd1319_ic_match);

static struct platform_driver rtd1319_ic_driver = {
	.probe = rtd1319_ic_probe,
	.driver = {
		.name = "rtk-rtd1319-iso-clk",
		.of_match_table = of_match_ptr(rtd1319_ic_match),
	},
};

static int __init rtd1319_ic_init(void)
{
	return platform_driver_register(&rtd1319_ic_driver);
}
subsys_initcall(rtd1319_ic_init);
MODULE_DESCRIPTION("Reatek RTD1319 ISO Controller Driver");
MODULE_AUTHOR("Cheng-Yu Lee <cylee12@realtek.com>");
MODULE_LICENSE("GPL v2");
