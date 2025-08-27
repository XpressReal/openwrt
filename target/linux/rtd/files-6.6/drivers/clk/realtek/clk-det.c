// SPDX-License-Identifier: GPL-2.0-only

#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <soc/realtek/rtk_clk_det.h>

struct clk_det {
	struct device *dev;
	void __iomem *base;
	struct clk_hw hw;
	int ofs;
	int type;
	struct clk *ref;
};

struct clk_det_desc {
	u32 ctrl_rstn_bit;
	u32 ctrl_cnten_bit;
	u32 stat_ofs;
	u32 stat_done_bit;
	u32 cnt_mask;
	u32 cnt_shift;
	u32 no_polling_done;
};

static const struct clk_det_desc clk_det_descs[3] = {
	[CLK_DET_TYPE_GENERIC] = {
		.ctrl_rstn_bit = 0,
		.ctrl_cnten_bit = 1,
		.stat_ofs = 0x0,
		.stat_done_bit = 30,
		.cnt_mask = GENMASK(29, 13),
		.cnt_shift = 13,
		.no_polling_done = 0,
	},
	[CLK_DET_TYPE_SC_WRAP] = {
		.ctrl_rstn_bit = 17,
		.ctrl_cnten_bit = 16,
		.stat_ofs = 0x8,
		.stat_done_bit = 0,
		.cnt_mask = GENMASK(17, 1),
		.cnt_shift = 1,
	},
	[CLK_DET_TYPE_HDMI_TOP] = {
		.ctrl_rstn_bit = 0,
		.ctrl_cnten_bit = 1,
		.stat_ofs = 0x0,
		.cnt_mask = GENMASK(29, 13),
		.cnt_shift = 13,
		.no_polling_done = 1,
	}
};

static DEFINE_MUTEX(clk_det_lock);

static void clk_det_update_bits(struct clk_det *clkd, u32 ofs, u32 mask, u32 val)
{
	u32 reg = readl(clkd->base + ofs);

	reg &= ~mask;
	reg |= (mask & val);
	writel(reg, clkd->base + ofs);
}

static void clk_det_read(struct clk_det *clkd, u32 ofs, u32 *val)
{
	*val = readl(clkd->base + ofs);
}

static unsigned long clk_det_get_freq(struct clk_det *clkd)
{
	const struct clk_det_desc *desc = &clk_det_descs[clkd->type];
	u32 ctrl_mask;
	u32 val;
	unsigned long freq = 0;
	int ret = 0;

	mutex_lock(&clk_det_lock);

	ctrl_mask = BIT(desc->ctrl_rstn_bit) | BIT(desc->ctrl_cnten_bit);
	clk_det_update_bits(clkd, 0, ctrl_mask, 0);
	clk_det_update_bits(clkd, 0, ctrl_mask, BIT(desc->ctrl_rstn_bit));
	clk_det_update_bits(clkd, 0, ctrl_mask, ctrl_mask);

	if (desc->no_polling_done)
		msleep(10);
	else
		ret = readl_poll_timeout(clkd->base + desc->stat_ofs, val, val & BIT(desc->stat_done_bit),
			0, 100);
	if (!ret) {
		clk_det_read(clkd, desc->stat_ofs, &val);
		freq = ((val & desc->cnt_mask) >> desc->cnt_shift) * 100000;
	}

	clk_det_update_bits(clkd, 0, ctrl_mask, 0);

	mutex_unlock(&clk_det_lock);

	return freq;
}

static unsigned long clk_det_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct clk_det *clkd = container_of(hw, struct clk_det, hw);

	if (clkd->ref && !__clk_is_enabled(clkd->ref))
		return 0;

	return clk_det_get_freq(clkd);
}

static const struct clk_ops clk_det_ops = {
	.recalc_rate = clk_det_recalc_rate,
};

struct clk *devm_clk_det_register(struct device *dev, const struct clk_det_initdata *initdata)
{
	struct clk_det *clkd;
	struct clk_init_data clk_initdata = { .name = initdata->name, .ops = &clk_det_ops,
		.flags = CLK_GET_RATE_NOCACHE };
	int ret;
	struct clk_hw *hw;

	clkd = devm_kzalloc(dev, sizeof(*clkd), GFP_KERNEL);
	if (!clkd)
		return ERR_PTR(-ENOMEM);

	clkd->dev = dev;
	clkd->base    = initdata->base;
	clkd->type    = initdata->type;
	clkd->ref     = initdata->ref;

	hw = &clkd->hw;
	hw->init = &clk_initdata;
	ret = devm_clk_hw_register(dev, hw);
	if (ret) {
		return ERR_PTR(ret);
	}

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get, hw);
	if (ret) {
		dev_err(dev, "failed to add clk provider: %d\n", ret);
		return ERR_PTR(ret);
	}
	return hw->clk;
}
EXPORT_SYMBOL_GPL(devm_clk_det_register);

static int of_clk_det_get_initdata(struct device *dev, struct clk_det_initdata *data)
{
	struct device_node *np = dev->of_node;
	int ret;

	data->ref = devm_clk_get_optional(dev, 0);
	if (IS_ERR(data->ref))
		return dev_err_probe(dev, PTR_ERR(data->ref), "failed to get clk\n");

	ret = of_property_read_string_index(np, "clock-output-names", 0, &data->name);
	if (ret)
		return ret;

	of_property_read_u32(np, "realtek,clk-det-type", &data->type);

	return 0;
}

static int clk_det_plat_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct clk_det_initdata initdata = { 0 };
	int ret;
	struct clk *clk;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;
	initdata.base = devm_ioremap(dev, res->start, resource_size(res));
	if (!initdata.base)
		return -ENOMEM;

	ret = of_clk_det_get_initdata(dev, &initdata);
	if (ret)
		return ret;

	clk = devm_clk_det_register(dev, &initdata);
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		dev_err(dev, "failed to create clk_det: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct of_device_id clk_det_match[] = {
	{ .compatible = "realtek,clk-det", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, clk_det_match);

static struct platform_driver clk_det_driver = {
	.probe = clk_det_plat_probe,
	.driver = {
		.name = "rtk-clk-det",
		.of_match_table = of_match_ptr(clk_det_match),
	},
};
module_platform_driver(clk_det_driver);

MODULE_LICENSE("GPL v2");
