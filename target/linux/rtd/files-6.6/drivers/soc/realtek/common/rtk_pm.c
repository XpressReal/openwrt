// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Realtek DHC SoC family power management driver
 * Copyright (c) 2020-2021 Realtek Semiconductor Corp.
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/regmap.h>
#include <linux/suspend.h>
#include <linux/dma-mapping.h>
#include <linux/clk.h>

#include <soc/realtek/rtk_pm.h>

unsigned int iso_en;
unsigned int msic_en;

u32 dco_mode;

static void rtk_pm_get_gpio_param(struct pm_private *dev_pm)
{
	struct device *dev = dev_pm->dev;
	struct pm_pcpu_param *pcpu_param = dev_pm->pcpu_param;
	int num_row = 0;
	int i = 0;
	int ret = 0;
	u32 gpio_act = 0;
	u32 gpio_en = 0;
	u32 gpio_num = 0;
	char *wu_list_prop = "wakeup-gpio-list";
	char *bt_gpio_prop = "rtk-bt";
	const u32 element = 3;

	num_row = of_property_count_u32_elems(dev->of_node, wu_list_prop);
	if (num_row < 0) {
		dev_err(dev, "Not found '%s' property\n", wu_list_prop);
		return;
	}

	num_row /= element;
	for (i = 0; i < num_row; i++) {
		of_property_read_u32_index(dev->of_node, wu_list_prop,
					   i * element, &gpio_num);
		of_property_read_u32_index(dev->of_node, wu_list_prop,
					   i * element + 1, &gpio_en);
		of_property_read_u32_index(dev->of_node, wu_list_prop,
					   i * element + 2, &gpio_act);
		pcpu_param->wu_gpio_en[gpio_num] = (char) gpio_en;
		pcpu_param->wu_gpio_act[gpio_num] = (char)gpio_act;
	}

	pcpu_param->bt = 255;
	ret = of_property_read_u32(dev->of_node, bt_gpio_prop, &pcpu_param->bt);
	if (ret) {
		dev_err(dev, "Not found '%s' property\n", bt_gpio_prop);
		return;
	}
}

static void rtk_pm_shutdown(struct platform_device *pdev)
{
	struct pm_private *dev_pm = dev_get_drvdata(&pdev->dev);
	int ret = 0;

	if (dco_mode == true)
		ret = clk_prepare_enable(dev_pm->dco);

	rtk_pm_set_pcpu_param(&pdev->dev);
};

static int rtk_pm_prepare(struct device *dev)
{
	struct pm_private *dev_pm = dev_get_drvdata(dev);
	struct pm_dev_param *lan_node = rtk_pm_get_param(LAN);
	int ret = 0;

	if (lan_node == NULL)
		goto skip_set_dco_param;

	if (dev_pm->pcpu_param->wakeup_source & DCO_ENABLE) {
		*(int *)lan_node->data = DCO_ENABLE;
		dev_pm->pcpu_param->wakeup_source &= 0xfffffffe;
	} else
		*(int *)lan_node->data = 0;

skip_set_dco_param:

	dev_pm->wakeup_reason = MAX_EVENT;
	dev_pm->device_param->data = &dev_pm->wakeup_reason;

	return ret;
}

static void rtk_pm_set_irqmux(unsigned int state)
{
	void __iomem *iso_irq_en_base;
	void __iomem *misc_irq_en_base;
	void __iomem *iso_irq_isr_base;
	void __iomem *misc_irq_isr_base;

	iso_irq_en_base = ioremap(0x98007040, 0x4);
	misc_irq_en_base = ioremap(0x9801b080, 0x4);
	iso_irq_isr_base = ioremap(0x98007000, 0x8);
	misc_irq_isr_base = ioremap(0x9801b008, 0x8);

	if (state == DISABLE_IRQMUX) {
		iso_en = readl(iso_irq_en_base);
		msic_en = readl(misc_irq_en_base);
		writel(0x0, iso_irq_en_base);
		writel(0x0, misc_irq_en_base);
		writel(0xfffffffe, iso_irq_isr_base);
		writel(0xfffffffe, misc_irq_isr_base);
		writel(0xfffffffe, iso_irq_isr_base + 0x4);
		writel(0xfffffffe, misc_irq_isr_base + 0x4);
	} else if (state == ENABLE_IRQMUX) {
		writel(0xfffffffe, iso_irq_isr_base);
		writel(0xfffffffe, misc_irq_isr_base);
		writel(0xfffffffe, iso_irq_isr_base + 0x4);
		writel(0xfffffffe, misc_irq_isr_base + 0x4);
		writel(iso_en, iso_irq_en_base);
		writel(msic_en, misc_irq_en_base);
	}

	iounmap(iso_irq_en_base);
	iounmap(misc_irq_en_base);
	iounmap(iso_irq_isr_base);
	iounmap(misc_irq_isr_base);
}

static int rtk_pm_suspend(struct device *dev)
{
	struct pm_private *dev_pm = dev_get_drvdata(dev);
	int ret = 0;

	if (dco_mode == true) {
		ret = clk_prepare_enable(dev_pm->dco);
		if (ret)
			return ret;
	}

	dev_pm->wakeup_reason = MAX_EVENT;
	regmap_read(dev_pm->syscon_iso, 0x640, &dev_pm->reboot_reasons);

	rtk_pm_set_pcpu_param(dev);

	rtk_pm_set_irqmux(DISABLE_IRQMUX);

	return ret;
}

static int rtk_pm_resume(struct device *dev)
{
	struct pm_private *dev_pm = dev_get_drvdata(dev);
	struct pm_pcpu_param *pcpu_param = dev_pm->pcpu_param;
	int ret = 0;

	pcpu_param->wakeup_source = htonl(pcpu_param->wakeup_source);
	pcpu_param->timerout_val = htonl(pcpu_param->timerout_val);

	regmap_read(dev_pm->syscon_iso, 0x640, &dev_pm->wakeup_reason);
	dev_pm->wakeup_reason = (dev_pm->wakeup_reason >> 16) & 0x00ff;

	regmap_write(dev_pm->syscon_iso, 0x640, dev_pm->reboot_reasons);

	dev_pm->device_param->data = &dev_pm->wakeup_reason;
	dev_pm->suspend_context++;

	rtk_pm_set_irqmux(ENABLE_IRQMUX);

	if (dco_mode == true)
		clk_disable_unprepare(dev_pm->dco);

	return ret;
}

static int rtk_pm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pm_private *dev_pm;
	struct pm_dev_param *dev_param;
	struct pm_pcpu_param *pcpu_param;
	int ret = 0;
	unsigned int test = 0;

	dev_pm = devm_kzalloc(dev, sizeof(*dev_pm), GFP_KERNEL);
	if (!dev_pm)
		return -ENOMEM;

	pcpu_param = dma_alloc_coherent(dev, sizeof(*pcpu_param), &dev_pm->pcpu_param_pa, GFP_KERNEL);
	if (!pcpu_param)
		return -ENOMEM;

	dev_param = devm_kzalloc(dev, sizeof(*dev_param), GFP_KERNEL);
	if (!dev_param)
		return -ENOMEM;

	dco_mode = 0;

	dev_pm->pm_dbg = false;
	dev_pm->dev = dev;

	dev_param->dev = dev;
	dev_param->dev_type = PM;

	pcpu_param->wakeup_source = 0;
	pcpu_param->timerout_val = 0;

	dev_pm->pcpu_param = pcpu_param;
	dev_pm->device_param = dev_param;

	dev_pm->syscon_iso = syscon_regmap_lookup_by_phandle(dev->of_node, "syscon");
	if (IS_ERR(dev_pm->syscon_iso)) {
		dev_err(dev, "Cannot get syscon\n");
		return PTR_ERR(dev_pm->syscon_iso);
	}

	ret = of_property_read_u32(dev->of_node, "wakeup-flags", &pcpu_param->wakeup_source);
	if (ret)
		dev_warn(dev, "Not found wakeup-flags property\n");

	dev_pm->pm_dbg = of_property_read_bool(dev->of_node, "pm-dbg");

	ret = of_property_read_u32(dev->of_node, "wakeup-timer", &pcpu_param->timerout_val);
	if (ret)
		dev_warn(dev, "Not found wakeup-timer property\n");

	dev_pm->dco = devm_clk_get(dev, "dco");
	if (IS_ERR(dev_pm->dco)) {
		dev_warn(dev, "failded to get clk_dco: %ld\n", PTR_ERR(dev_pm->dco));
		dev_pm->dco = NULL;
	} else {
		ret = of_property_read_u32(dev->of_node, "dco", &dco_mode);
		if (ret) {
			dev_warn(dev, "Not found dco property\n");
		} else {
			if (dco_mode == true) {
				pcpu_param->wakeup_source &= 0xfffffffe;
				pcpu_param->wakeup_source |= DCO_ENABLE;
			}
		}
	}

	ret = rtk_pm_create_sysfs();
	if (ret)
		dev_err(dev, "Cannot create sysfs\n");

	regmap_read(dev_pm->syscon_iso, 0x640, &dev_pm->wakeup_reason);
	test = (dev_pm->wakeup_reason >> 16) & 0x00ff;
	if (test > MAX_EVENT ||  test < IR_EVENT)
		dev_pm->wakeup_reason = MAX_EVENT;
	else
		dev_pm->wakeup_reason = test;

	dev_param->data = &dev_pm->wakeup_reason;

	rtk_pm_get_gpio_param(dev_pm);
	rtk_pm_add_list(dev_param);
	platform_set_drvdata(pdev, dev_pm);

	return 0;
}

static const struct dev_pm_ops rtk_pm_ops = {
	.prepare = rtk_pm_prepare,
	.suspend_noirq = rtk_pm_suspend,
	.resume_noirq = rtk_pm_resume,
};

static struct of_device_id rtk_pm_ids[] = {
	{ .compatible = "realtek,rtd13xx_pm" },
	{ .compatible = "realtek,rtd13xxd_pm" },
	{ .compatible = "realtek,rtd16xxb_pm" },
	{ .compatible = "realtek,rtk_pm" },
	{ /* Sentinel */ },
};

static struct platform_driver rtk_pm_driver = {
	.probe = rtk_pm_probe,
	.shutdown = rtk_pm_shutdown,
	.driver = {
		.name = "realtek-pm",
		.owner = THIS_MODULE,
		.of_match_table = rtk_pm_ids,
		.pm = &rtk_pm_ops,
		.suppress_bind_attrs = true,
	},
};
module_platform_driver(rtk_pm_driver);

MODULE_AUTHOR("James Tai <james.tai@realtek.com>");
MODULE_DESCRIPTION("Realtek power management driver");
MODULE_LICENSE("GPL v2");
