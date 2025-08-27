// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2019 Realtek Semiconductor Corp.
 */

#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <soc/realtek/rtk_sb2_inv.h>

#define SB2_INV_INTEN              0x0
#define SB2_INV_INTSTAT            0x4
#define SB2_INV_ADDR               0x8
#define SB2_DEBUG_REG              0xC

#define SB2_INV_IRQEN_SCPU         (1 << 1)
#define SB2_INV_IRQEN_PCPU         (1 << 2)
#define SB2_INV_IRQEN_ACPU         (1 << 3)
#define SB2_INV_IRQEN_SCPU_SWC     (1 << 6)
#define SB2_INV_IRQEN_PCPU_2       (1 << 8)
#define SB2_INV_IRQEN_VCPU         (1 << 10)
#define SB2_INV_IRQEN_AUCPU0       (1 << 11)
#define SB2_INV_IRQEN_AUCPU1       (1 << 12)
#define DEFAULT_IRQEN_CPUS \
	(SB2_INV_IRQEN_SCPU | SB2_INV_IRQEN_PCPU | SB2_INV_IRQEN_SCPU_SWC)

struct sb2_inv_drvdata {
	struct device *dev;
	void *base;
};

static const char *cpu_id_string[] = {
	[SB2_INV_CPU_ID_UNKNOWN]  = "Unknown CPU",
	[SB2_INV_CPU_ID_SCPU]     = "SCPU",
	[SB2_INV_CPU_ID_PCPU]     = "PCPU",
	[SB2_INV_CPU_ID_ACPU]     = "ACPU",
	[SB2_INV_CPU_ID_SCPU_SWC] = "SCPU security world",
	[SB2_INV_CPU_ID_PCPU_2]   = "PCPU/R-BUS2",
	[SB2_INV_CPU_ID_VCPU]     = "VCPU",
	[SB2_INV_CPU_ID_AUCPU0]   = "AUCPU0",
	[SB2_INV_CPU_ID_AUCPU1]   = "AUCPU0",
	[SB2_INV_CPU_ID_HIF]      = "HIF",
};

static int cpu_reg_map[32] = {
	[1] = SB2_INV_CPU_ID_SCPU,
	[2] = SB2_INV_CPU_ID_PCPU,
	[3] = SB2_INV_CPU_ID_ACPU,
	[4] = SB2_INV_CPU_ID_SCPU_SWC,
	[5] = SB2_INV_CPU_ID_PCPU_2,
	[6] = SB2_INV_CPU_ID_VCPU,
	[7] = SB2_INV_CPU_ID_AUCPU0,
	[8] = SB2_INV_CPU_ID_AUCPU1,
	[9] = SB2_INV_CPU_ID_HIF,
};

static const char *get_cpu_id(int id)
{
	if (id >= SB2_INV_CPU_ID_MAX)
		id = 0;
	return cpu_id_string[id];
}

static void sb2_inv_print_inv_event(struct sb2_inv_event_data *evt)
{
	pr_err("sb2 get int 0x%08x from SB2_INV_INTSTAT\n", evt->raw_ints);
	pr_err("\033[0;31mInvalid access issued by %s\033[m\n", get_cpu_id(evt->inv_cpu));
	pr_err("\033[0;31mInvalid address is 0x%08x\033[m\n", evt->addr);
	pr_err("Timeout threshold(0x%08x)\n", evt->timeout_th);
}

static irqreturn_t sb2_inv_int_handler(int irq, void *id)
{
	struct platform_device *pdev = id;
	struct sb2_inv_drvdata *sb2 = platform_get_drvdata(pdev);
	u32 ints;
	struct sb2_inv_event_data evt = {0};

	ints = readl(sb2->base + SB2_INV_INTSTAT);
	if (!ints)
		return IRQ_NONE;

	/* clear ints */
	writel(~1, sb2->base + SB2_INV_INTSTAT);

	evt.raw_ints   = ints;
	evt.inv_cpu    = cpu_reg_map[ffs(ints) - 1];
	evt.addr       = readl(sb2->base + SB2_INV_ADDR);
	evt.timeout_th = readl(sb2->base + SB2_DEBUG_REG);

	sb2_inv_print_inv_event(&evt);

	return IRQ_HANDLED;
}

static void sb2_inv_enable_inv_int(struct sb2_inv_drvdata *sb2)
{
	writel(~1, sb2->base + SB2_INV_INTSTAT);
	writel(1 | DEFAULT_IRQEN_CPUS, sb2->base + SB2_INV_INTEN);
}

static int sb2_inv_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sb2_inv_drvdata *sb2;
	struct device_node *np = dev->of_node;
	struct resource res;
	int ret;
	int irq;

	sb2 = devm_kzalloc(dev, sizeof(*sb2), GFP_KERNEL);
	if (!sb2)
		return -ENOMEM;
	sb2->dev = dev;

	ret = of_address_to_resource(np, 0, &res);
	if (ret)
		return ret;

	sb2->base = devm_ioremap(dev, res.start, resource_size(&res));
	if (!sb2->base)
		return -ENOMEM;

	platform_set_drvdata(pdev, sb2);

	irq = irq_of_parse_and_map(np, 0);
	if (irq < 0) {
		dev_err(dev, "failed to parse irq: %d\n", irq);
		return -ENXIO;
	}

	ret = devm_request_irq(dev, irq, sb2_inv_int_handler, IRQF_SHARED,
			       dev_name(dev), pdev);
	if (ret) {
		dev_err(dev, "failed to request irq: %d\n", ret);
		return -ENXIO;
	}

	sb2_inv_enable_inv_int(sb2);

	return 0;
}

static int sb2_inv_resume(struct device *dev)
{
	struct sb2_inv_drvdata *sb2 = dev_get_drvdata(dev);

	dev_info(dev, "enter %s\n", __func__);
	sb2_inv_enable_inv_int(sb2);
	dev_info(dev, "exit %s\n", __func__);
	return 0;
}

static struct dev_pm_ops sb2_inv_pm_ops = {
	.resume_noirq  = sb2_inv_resume,
};

static const struct of_device_id sb2_inv_match[] = {
	{.compatible = "realtek,sysbrg2-inv"},
	{},
};
MODULE_DEVICE_TABLE(of, sb2_inv_match);

static struct platform_driver sb2_inv_driver = {
	.probe  = sb2_inv_probe,
	.driver = {
		.owner          = THIS_MODULE,
		.name           = "rtk-sb2-inv",
		.pm             = &sb2_inv_pm_ops,
		.of_match_table = of_match_ptr(sb2_inv_match),
	},
};

static int __init rtk_sb2_init(void)
{
	return platform_driver_register(&sb2_inv_driver);
}
subsys_initcall_sync(rtk_sb2_init);

MODULE_DESCRIPTION("Realtek SB2 Invaild Access driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:rtk-sb2-inv");
MODULE_AUTHOR("Cheng-Yu Lee <cylee12@realtek.com>");
