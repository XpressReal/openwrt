// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Realtek Semiconductor Corp.
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/printk.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/sys_soc.h>

struct rtk_gpu_wrap_desc;

struct rtk_gpu_wrap_data {
	struct device *dev;
	struct clk *clk;
	struct clk *clk_ts_src;
	struct reset_control *rstc;
	struct reset_control *rstc_bist;
	void __iomem *reg_bist;
	struct regmap *dbgprot;
	int clk_cnt;
	const struct rtk_gpu_wrap_desc *desc;
	u32 auto_bist_enabled : 1;
	u32 check_bisr_error : 1;
};

struct rtk_gpu_wrap_desc {
	u32 has_bisr : 1;
	u32 request_syscon_dbgport : 1;
	u32 request_nvmem_bist_rst_ctrl : 1;
	int (*setup_bisr)(struct rtk_gpu_wrap_data *data);
	int (*do_bisr)(struct rtk_gpu_wrap_data *data);
};

static void rtd1619b_gpu_wrap_sram_fixup(struct rtk_gpu_wrap_data *data)
{
	u32 val = readl(data->reg_bist + 0xb0);

	writel(val | 0x8051c364, data->reg_bist + 0xb0);
	writel(val & ~0x8051c364, data->reg_bist + 0xb0);
}

static int rtd1619b_do_bisr(struct rtk_gpu_wrap_data *data)
{
	unsigned int regval;
	int ret;

	rtd1619b_gpu_wrap_sram_fixup(data);

	if (!data->auto_bist_enabled)
		writel(0x00003030, data->reg_bist + 0x10);

	ret = readl_poll_timeout(data->reg_bist + 0x40, regval, (regval & 0x1001) == 0x1001, 0, 1000000);
	if (ret || regval != 0x1001)
		dev_warn(data->dev, "error %pe: bisr failed: status=%08x\n", ERR_PTR(ret), regval);
	return ret;
}

static const struct rtk_gpu_wrap_desc rtd1619b_gpu_wrap_desc = {
	.has_bisr = 1,
	.request_nvmem_bist_rst_ctrl = 1,
	.do_bisr = rtd1619b_do_bisr,
};

static int rtd1319d_auto_bist_enabled(struct rtk_gpu_wrap_data *data)
{
	unsigned int st1, st2;

	if (!data->dbgprot)
		return 0;

	regmap_read(data->dbgprot, 0, &st1);
	regmap_read(data->dbgprot, 4, &st2);
	st2 >>= 4;

	if ((st1 == 0x1c && st2 == 0x1) || (st1 == 0x01 && st2 == 0x2))
		return 0;

	return 1;
}

static int rtd1319d_setup_bisr(struct rtk_gpu_wrap_data *data)
{
	const struct soc_device_attribute match[] = {
		{ .family = "Realtek Parker", .revision = "A01", },
		{}
	};

	if (rtd1319d_auto_bist_enabled(data)) {
		dev_info(data->dev, "with auto bist\n");
		data->auto_bist_enabled = 1;
	}


	if (soc_device_match(match) != NULL) {
		dev_info(data->dev, "with checking bist error\n");
		data->check_bisr_error = 1;
	}

	return 0;
}

static int rtd1319d_do_bisr(struct rtk_gpu_wrap_data *data)
{
	unsigned int regval;
	int ret;

	if (!data->auto_bist_enabled)
		writel(0x00000031, data->reg_bist + 0x10);
	ret = readl_poll_timeout(data->reg_bist + 0x40, regval, (regval & 0x1) == 0x1, 0, 1000000);
	if (ret || (data->check_bisr_error && regval != 1))
		dev_warn(data->dev, "error %pe: bisr failed: status=%08x\n", ERR_PTR(ret), regval);
	return ret;
}

static const struct rtk_gpu_wrap_desc rtd1319d_gpu_wrap_desc = {
	.has_bisr = 1,
	.request_syscon_dbgport = 1,
	.setup_bisr = rtd1319d_setup_bisr,
	.do_bisr = rtd1319d_do_bisr,
};

static const struct rtk_gpu_wrap_desc rtd1315e_gpu_wrap_desc = {
};

static int rtk_gpu_wrap_setup_bisr(struct rtk_gpu_wrap_data *data)
{
	const struct rtk_gpu_wrap_desc *desc = data->desc;

	if (!desc->setup_bisr)
		return 0;

	return desc->setup_bisr(data);
}

static void rtk_gpu_wrap_bisr(struct rtk_gpu_wrap_data *data)
{
	const struct rtk_gpu_wrap_desc *desc = data->desc;
	ktime_t start;
	s64 delta_us;
	int ret;

	if (!desc->do_bisr)
		return;

	start = ktime_get();
	ret = desc->do_bisr(data);
	delta_us = ktime_to_us(ktime_sub(ktime_get(), start));

	dev_dbg(data->dev, "auto_bist_enabled=%d, bisr takes %lldus\n", data->auto_bist_enabled, delta_us);
}

static int rtk_gpu_wrap_power_on(struct rtk_gpu_wrap_data *data)
{
	reset_control_deassert(data->rstc);
	reset_control_deassert(data->rstc_bist);
	clk_prepare_enable(data->clk_ts_src);
	clk_prepare_enable(data->clk);

	dmb(sy); /* for rtd1319d/rtd1315e rbus/rbus5 sync */

	rtk_gpu_wrap_bisr(data);

	return 0;
}

static int rtk_gpu_wrap_power_off(struct rtk_gpu_wrap_data *data)
{
	dmb(sy); /* for rtd1319d/rtd1315e rbus/rbus5 sync */

	/*
	 * Since mali driver checks __clk_is_enabled() to perform clk_disable_unprepare() in
	 * power_control_term(), check again to prevent WARNING.
	 */
	if (__clk_is_enabled(data->clk))
		clk_disable_unprepare(data->clk);
	clk_disable_unprepare(data->clk_ts_src);
	reset_control_assert(data->rstc);
	reset_control_assert(data->rstc_bist);
	return 0;
}

static int rtk_gpu_wrap_runtime_suspend(struct device *dev)
{
	struct rtk_gpu_wrap_data *data = dev_get_drvdata(dev);

	dev_dbg(dev, "%s\n", __func__);
	rtk_gpu_wrap_power_off(data);
	return 0;
}

static int rtk_gpu_wrap_runtime_resume(struct device *dev)
{
	struct rtk_gpu_wrap_data *data = dev_get_drvdata(dev);

	dev_dbg(dev, "%s\n", __func__);
	rtk_gpu_wrap_power_on(data);
	return 0;
}

static int rtk_gpu_wrap_force_clk_disable(struct rtk_gpu_wrap_data *data)
{
	int i = 0;

	/* gpu may enter suspend, with clk enabled, disable it */
	if (__clk_is_enabled(data->clk) && i < 10) {
		i++;
		clk_disable_unprepare(data->clk);
	}
	return i;
}

static int rtk_gpu_wrap_suspend(struct device *dev)
{
	struct rtk_gpu_wrap_data *data = dev_get_drvdata(dev);
	int ret;

	dev_info(dev, "enter %s\n", __func__);

	ret = pm_runtime_force_suspend(dev);
	if (ret) {
		dev_err(dev, "failed to force suspend: %d\n", ret);
		return ret;
	}

	data->clk_cnt = rtk_gpu_wrap_force_clk_disable(data);
	if (data->clk_cnt)
		dev_warn(dev, "do clk_disable_unprepare %d times\n", data->clk_cnt);

	dev_info(dev, "exit %s\n", __func__);
	return 0;
}

static int rtk_gpu_wrap_resume(struct device *dev)
{
	struct rtk_gpu_wrap_data *data = dev_get_drvdata(dev);
	int i;

	dev_info(dev, "enter %s\n", __func__);

	pm_runtime_force_resume(dev);

	for (i = 0; i < data->clk_cnt; i++)
		clk_prepare_enable(data->clk);

	dev_info(dev, "exit %s\n", __func__);
	return 0;
}

static const struct dev_pm_ops rtk_gpu_wrap_dev_pm_ops = {
	.runtime_resume  = rtk_gpu_wrap_runtime_resume,
	.runtime_suspend = rtk_gpu_wrap_runtime_suspend,
	.suspend         = rtk_gpu_wrap_suspend,
	.resume          = rtk_gpu_wrap_resume,
};

static int rtk_gpu_wrap_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rtk_gpu_wrap_data *data;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	data->dev = dev;
	data->desc = of_device_get_match_data(dev);
	if (!data->desc) {
		dev_err(dev, "failed to get gpu_wrap desc\n");
		return -EINVAL;
	}

	if (data->desc->request_nvmem_bist_rst_ctrl) {
		struct nvmem_cell *cell;
	        unsigned char *buf;
		size_t buf_size;

		cell = nvmem_cell_get(dev, "bist_rst_ctrl");
		if (IS_ERR(cell))
			return dev_err_probe(dev, PTR_ERR(cell), "failed to get bist-rst-ctrl");

		buf = nvmem_cell_read(cell, &buf_size);
		dev_info(dev, "bist-rst-ctrl=%d\n", buf[0]);
		if (buf[0] == 0x2)
			data->auto_bist_enabled = 1;
		kfree(buf);
		nvmem_cell_put(cell);
	}

	if (data->desc->has_bisr) {
		data->reg_bist = devm_platform_ioremap_resource_byname(pdev, "bist");
		if (IS_ERR(data->reg_bist))
			return PTR_ERR(data->reg_bist);
	}

	data->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(data->clk))
		return dev_err_probe(dev, PTR_ERR(data->clk), "failed to get clk\n");

	data->clk_ts_src = devm_clk_get_optional(dev, "ts_src");
	if (IS_ERR(data->clk_ts_src))
		return dev_err_probe(dev, PTR_ERR(data->clk_ts_src), "failed to get clk ts_src\n");

	data->rstc = devm_reset_control_get_exclusive(dev, NULL);
	if (IS_ERR(data->rstc))
		return dev_err_probe(dev, PTR_ERR(data->rstc), "failed to get reset\n");

	data->rstc_bist = devm_reset_control_get_exclusive(dev, "bist");
	if (IS_ERR(data->rstc_bist))
		return dev_err_probe(dev, PTR_ERR(data->rstc_bist), "failed to get bist reset\n");

	if (data->desc->request_syscon_dbgport) {
		data->dbgprot = syscon_regmap_lookup_by_phandle(dev->of_node, "realtek,dbgprot");
		if (IS_ERR(data->dbgprot)) {
			dev_warn(dev, "error %pe: failed to get dbgprot syscon\n", data->dbgprot);
			data->dbgprot = NULL;
		}
	}

	if (data->desc->has_bisr)
		rtk_gpu_wrap_setup_bisr(data);

	platform_set_drvdata(pdev, data);

	pm_runtime_set_suspended(dev);
	pm_runtime_enable(dev);

	return of_platform_populate(dev->of_node, NULL, NULL, dev);
}

static int rtk_gpu_wrap_remove(struct platform_device *pdev)
{
	of_platform_depopulate(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static void rtk_gpu_wrap_shutdown(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rtk_gpu_wrap_data *data = platform_get_drvdata(pdev);

	dev_info(dev, "enter %s\n", __func__);
	pm_runtime_force_suspend(dev);
	rtk_gpu_wrap_force_clk_disable(data);
	dev_info(dev, "exit %s\n", __func__);
}

static const struct of_device_id rtk_gpu_wrap_match[] = {
	{ .compatible = "realtek,rtd1619b-gpu-wrap", .data = &rtd1619b_gpu_wrap_desc, },
	{ .compatible = "realtek,rtd1319d-gpu-wrap", .data = &rtd1319d_gpu_wrap_desc, },
	{ .compatible = "realtek,rtd1315e-gpu-wrap", .data = &rtd1315e_gpu_wrap_desc, },
	{ .compatible = "realtek,rtd1625-gpu-wrap", .data = &rtd1315e_gpu_wrap_desc, },
	{}
};

static struct platform_driver rtk_gpu_wrap_driver = {
	.probe    = rtk_gpu_wrap_probe,
	.remove   = rtk_gpu_wrap_remove,
	.shutdown = rtk_gpu_wrap_shutdown,
	.driver = {
		.owner          = THIS_MODULE,
		.name           = "rtk-gpu_wrap",
		.of_match_table = of_match_ptr(rtk_gpu_wrap_match),
		.pm             = &rtk_gpu_wrap_dev_pm_ops,
	},
};
module_platform_driver(rtk_gpu_wrap_driver);

MODULE_DESCRIPTION("Realtek GPU Wrapper driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:rtk-gpu_wrap");
MODULE_AUTHOR("Cheng-Yu Lee <cylee12@realtek.com>");
