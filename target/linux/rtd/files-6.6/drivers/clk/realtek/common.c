// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2019 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/pm.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/hwspinlock.h>
#include <linux/platform_device.h>
#include "common.h"

MODULE_LICENSE("GPL v2");

void clk_regmap_write(struct clk_regmap *clkr, u32 ofs, u32 val)
{
	pr_debug("%s: ofs=%03x, val=%08x\n", __func__, ofs, val);

	regmap_write(clkr->regmap, ofs, val);
}
EXPORT_SYMBOL_GPL(clk_regmap_write);

u32 clk_regmap_read(struct clk_regmap *clkr, u32 ofs)
{
	u32 val = 0;

	regmap_read(clkr->regmap, ofs, &val);
	pr_debug("%s: ofs=%03x, val=%08x\n", __func__, ofs, val);
	return val;
}
EXPORT_SYMBOL_GPL(clk_regmap_read);

void clk_regmap_update_bits(struct clk_regmap *clkr, u32 ofs, u32 mask, u32 val)
{
	pr_debug("%s: ofs=%03x, mask=%08x, val=%08x\n", __func__, ofs, mask, val);

	regmap_update_bits(clkr->regmap, ofs, mask, val);
}
EXPORT_SYMBOL_GPL(clk_regmap_update_bits);

static const struct regmap_config default_regmap_config = {
	.reg_bits       = 32,
	.reg_stride     = 4,
	.val_bits       = 32,
	.max_register   = 0xffc,
	.fast_io        = true,
};

struct rtk_clk_drvdata {
	const struct rtk_clk_desc *desc;
	void __iomem *base;
	struct regmap *regmap;
};

static int rtk_clk_setup_map(struct platform_device *pdev, struct rtk_clk_drvdata *data)
{
	struct resource *res;
	struct device *dev = &pdev->dev;
	struct regmap_config cfg = default_regmap_config;
	int ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;
	data->base = devm_ioremap(dev, res->start, resource_size(res));
	if (!data->base)
		return -ENOMEM;

	ret = of_hwspin_lock_get_id(dev->of_node, 0);
	if (ret > 0 || (IS_ENABLED(CONFIG_HWSPINLOCK) && ret == 0)) {
		cfg.use_hwlock = true;
		cfg.hwlock_id = ret;
		cfg.hwlock_mode = HWLOCK_IRQSTATE;
	} else if (ret < 0) {
		switch (ret) {
		case -ENOENT:
			/* Ignore missing hwlock, it's optional. */
			break;
		default:
			pr_err("Failed to retrieve valid hwlock: %d\n", ret);
			fallthrough;
		case -EPROBE_DEFER:
			return ret;
		}
	}

	cfg.max_register = resource_size(res) - 4;
	data->regmap = devm_regmap_init_mmio(dev, data->base, &cfg);
	return PTR_ERR_OR_ZERO(data->regmap);
}

int rtk_clk_probe(struct platform_device *pdev, const struct rtk_clk_desc *desc)
{
	int i;
	struct device *dev = &pdev->dev;
	int ret;
	struct rtk_reset_initdata reset_initdata = {0};
	struct rtk_clk_drvdata *data;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->desc = desc;
	ret = rtk_clk_setup_map(pdev, data);
	if (ret)
		return ret;

	for (i = 0; i < desc->num_clks; i++)
		desc->clks[i]->regmap = data->regmap;

	for (i = 0; i < desc->clk_data->num; i++) {
		struct clk_hw *hw = desc->clk_data->hws[i];
		if (!hw)
			continue;
		ret = devm_clk_hw_register(dev, hw);
		if (ret)
			dev_warn(dev, "failed to register hw%d/%s: %d\n", i, clk_hw_get_name(hw), ret);
	}

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get, desc->clk_data);
	if (ret)
		return ret;

	if (!desc->num_reset_banks)
		return 0;

	reset_initdata.regmap = data->regmap;
	reset_initdata.num_banks = desc->num_reset_banks;
	reset_initdata.banks = desc->reset_banks;
	return rtk_reset_controller_add(dev, &reset_initdata);
}
EXPORT_SYMBOL_GPL(rtk_clk_probe);
