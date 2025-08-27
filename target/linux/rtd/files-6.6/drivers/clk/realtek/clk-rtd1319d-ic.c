// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */

#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <dt-bindings/clock/rtd1319d-clk.h>
#include "common.h"

static struct clk_dco_data clk_dco = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_dco", &clk_dco_ops, 0),
	.pll_etn_osc = &clk_dco.clkr,
};

static struct clk_regmap rtd1315e_pll_etn_osc;

static struct clk_dco_data rtd1315e_clk_dco = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_dco", &clk_dco_ops, 0),
	.pll_etn_osc = &rtd1315e_pll_etn_osc,
	.loc = 1,
};

static struct clk_regmap_gate clk_en_lsadc = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_lsadc", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x8c,
	.bit_idx = 0,
};

static struct clk_regmap_gate clk_en_iso_gspi = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_iso_gspi", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x8c,
	.bit_idx = 1,
};

static struct clk_regmap_gate clk_en_misc_cec0 = {
	.clkr.hw.init = CLK_HW_INIT("clk_en_misc_cec0", "clk_en_misc", &clk_regmap_gate_ops, 0),
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

static struct clk_regmap_gate clk_en_misc_ir = {
	.clkr.hw.init = CLK_HW_INIT("clk_en_misc_ir", "clk_en_misc", &clk_regmap_gate_ops, 0),
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

static struct clk_regmap_gate clk_en_vtc = {
	.clkr.hw.init = CLK_HW_INIT_NO_PARENT("clk_en_vtc", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x8c,
	.bit_idx = 17,
};

static struct clk_regmap_gate clk_en_misc_vfd = {
	.clkr.hw.init = CLK_HW_INIT("clk_en_misc_vfd", "clk_en_misc", &clk_regmap_gate_ops, 0),
	.gate_ofs = 0x8c,
	.bit_idx = 18,
};

static struct clk_regmap *rtd1319d_clk_regmap_list[] = {
	&clk_en_lsadc.clkr,
	&clk_en_iso_gspi.clkr,
	&clk_en_misc_cec0.clkr,
	&clk_en_cbusrx_sys.clkr,
	&clk_en_cbustx_sys.clkr,
	&clk_en_cbus_sys.clkr,
	&clk_en_cbus_osc.clkr,
	&clk_en_misc_ir.clkr,
	&clk_en_i2c0.clkr,
	&clk_en_i2c1.clkr,
	&clk_en_etn_250m.clkr,
	&clk_en_etn_sys.clkr,
	&clk_en_usb_drd.clkr,
	&clk_en_usb_host.clkr,
	&clk_en_usb_u3_host.clkr,
	&clk_en_usb.clkr,
	&clk_en_vtc.clkr,
	&clk_en_misc_vfd.clkr,
	&clk_dco.clkr,
	&rtd1315e_pll_etn_osc, /* only setup clk_regmap */
};

static struct clk_hw_onecell_data rtd1319d_iso_clk_data = {
	.num = RTD1319D_ISO_CLK_MAX,
	.hws = {
		[RTD1319D_ISO_CLK_EN_LSADC] = &__clk_regmap_gate_hw(&clk_en_lsadc),
		[RTD1319D_ISO_CLK_EN_ISO_GSPI] = &__clk_regmap_gate_hw(&clk_en_iso_gspi),
		[RTD1319D_ISO_CLK_EN_MISC_CEC0] = &__clk_regmap_gate_hw(&clk_en_misc_cec0),
		[RTD1319D_ISO_CLK_EN_CBUSRX_SYS] = &__clk_regmap_gate_hw(&clk_en_cbusrx_sys),
		[RTD1319D_ISO_CLK_EN_CBUSTX_SYS] = &__clk_regmap_gate_hw(&clk_en_cbustx_sys),
		[RTD1319D_ISO_CLK_EN_CBUS_SYS] = &__clk_regmap_gate_hw(&clk_en_cbus_sys),
		[RTD1319D_ISO_CLK_EN_CBUS_OSC] = &__clk_regmap_gate_hw(&clk_en_cbus_osc),
		[RTD1319D_ISO_CLK_EN_MISC_IR] = &__clk_regmap_gate_hw(&clk_en_misc_ir),
		[RTD1319D_ISO_CLK_EN_I2C0] = &__clk_regmap_gate_hw(&clk_en_i2c0),
		[RTD1319D_ISO_CLK_EN_I2C1] = &__clk_regmap_gate_hw(&clk_en_i2c1),
		[RTD1319D_ISO_CLK_EN_ETN_250M] = &__clk_regmap_gate_hw(&clk_en_etn_250m),
		[RTD1319D_ISO_CLK_EN_ETN_SYS] = &__clk_regmap_gate_hw(&clk_en_etn_sys),
		[RTD1319D_ISO_CLK_EN_USB_DRD] = &__clk_regmap_gate_hw(&clk_en_usb_drd),
		[RTD1319D_ISO_CLK_EN_USB_HOST] = &__clk_regmap_gate_hw(&clk_en_usb_host),
		[RTD1319D_ISO_CLK_EN_USB_U3_HOST] = &__clk_regmap_gate_hw(&clk_en_usb_u3_host),
		[RTD1319D_ISO_CLK_EN_USB] = &__clk_regmap_gate_hw(&clk_en_usb),
		[RTD1319D_ISO_CLK_EN_VTC] = &__clk_regmap_gate_hw(&clk_en_vtc),
		[RTD1319D_ISO_CLK_EN_MISC_VFD] = &__clk_regmap_gate_hw(&clk_en_misc_vfd),
		[RTD1319D_ISO_CLK_DCO] = &__clk_dco_hw(&clk_dco),
		[RTD1319D_ISO_CLK_MAX] = NULL,
	},
};

static struct rtk_reset_bank rtd1319d_iso_reset_banks[] = {
	{ .ofs = 0x88, },
};

static const struct rtk_clk_desc rtd1319d_iso_desc = {
	.clk_data        = &rtd1319d_iso_clk_data,
	.clks            = rtd1319d_clk_regmap_list,
	.num_clks        = ARRAY_SIZE(rtd1319d_clk_regmap_list),
	.reset_banks     = rtd1319d_iso_reset_banks,
	.num_reset_banks = ARRAY_SIZE(rtd1319d_iso_reset_banks),
};

static struct clk_hw_onecell_data rtd1315e_isosys_clk_data = {
	.num = 1,
	.hws = {
		[0] = &__clk_dco_hw(&rtd1315e_clk_dco),
	},
};

static struct clk_regmap *rtd1315e_isosys_clk_regmap_list[] = {
	&rtd1315e_clk_dco.clkr,
};

static const struct rtk_clk_desc rtd1315e_isosys_desc = {
	.clk_data = &rtd1315e_isosys_clk_data,
	.clks     = rtd1315e_isosys_clk_regmap_list,
	.num_clks = ARRAY_SIZE(rtd1315e_isosys_clk_regmap_list),
};

static int rtd1319d_iso_probe(struct platform_device *pdev)
{
	const struct rtk_clk_desc *desc;

	desc = of_device_get_match_data(&pdev->dev);
	if (!desc)
		return -EINVAL;

	/* clk_dco is located in isosys */
	if (of_device_is_compatible(pdev->dev.of_node, "realtek,rtd1315e-iso-clk"))
		rtd1319d_iso_clk_data.hws[RTD1319D_ISO_CLK_DCO] = NULL;

	return rtk_clk_probe(pdev, desc);
}

static const struct of_device_id rtd1319d_iso_match[] = {
	{ .compatible = "realtek,rtd1319d-iso-clk", .data = &rtd1319d_iso_desc, },
	{ .compatible = "realtek,rtd1315e-iso-clk", .data = &rtd1319d_iso_desc, },
	{ .compatible = "realtek,rtd1315e-isosys-clk", .data = &rtd1315e_isosys_desc, },
	{ /* sentinel */ }
};

static struct platform_driver rtd1319d_iso_driver = {
	.probe = rtd1319d_iso_probe,
	.driver = {
		.name = "rtk-rtd1319d-iso-clk",
		.of_match_table = rtd1319d_iso_match,
	},
};

static int __init rtd1319d_iso_init(void)
{
	return platform_driver_register(&rtd1319d_iso_driver);
}
subsys_initcall(rtd1319d_iso_init);

MODULE_DESCRIPTION("Realtek RTD1319D ISO Controller Driver");
MODULE_AUTHOR("Cheng-Yu Lee <cylee12@realtek.com>");
MODULE_LICENSE("GPL v2");
