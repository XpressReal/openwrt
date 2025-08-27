// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Realtek Semiconductor Corp.
 */

#include <linux/device.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/printk.h>
#include <linux/rtc.h>
#include <linux/slab.h>
#include <soc/realtek/rtk_pm.h>
#include <linux/timerqueue.h>

struct rtk_pm_alarm_data {
	struct device *dev;
	void *base;
	struct rtc_device *rtc;
	uint32_t unconsumed_overhead_ms;
	struct pm_private *pm_dev;

	uint32_t additional_overhead_ms;
	bool suspend_enabled;
	bool shutdown_enabled;
};

static int rtk_pm_alarm_get_next_wakeup(struct rtk_pm_alarm_data *data, time64_t *t)
{
	struct timerqueue_node *next = timerqueue_getnext(&data->rtc->timerqueue);
	struct rtc_time tm;
	ktime_t now, alm;

	if (!next)
		return -EINVAL;

	rtc_read_time(data->rtc, &tm);
	now = rtc_tm_to_ktime(tm);
	alm = next->expires;

	dev_dbg(data->dev, "%s: now=%lld,alm=%lld\n", __func__, now, alm);

	if (now >= alm)
		return -EINVAL;

	*t = ktime_ms_delta(alm, now);
	do_div(*t, 1000);
	return 0;
}

static void rtk_pm_alarm_advance_rtc_time(struct rtk_pm_alarm_data *data, time64_t t)
{
	struct rtc_time tm;

	rtc_read_time(data->rtc, &tm);

	rtc_time64_to_tm(rtc_tm_to_time64(&tm) + t, &tm);

	rtc_set_time(data->rtc, &tm);
}

static void rtk_pm_alarm_set_wakeup_time(struct rtk_pm_alarm_data *data, uint32_t t)
{
	writel(t, data->base);
}

static uint32_t rtk_pm_alarm_get_advanced_time(struct rtk_pm_alarm_data *data)
{
	return readl(data->base);
}

static int rtk_pm_alarm_setup(struct rtk_pm_alarm_data *data, bool enable)
{
	time64_t t = 0;
	int ret;

	rtk_pm_wakeup_source_alarm_set(data->pm_dev, 0);

	if (!enable) {
		dev_dbg(data->dev, "alarm disabled\n");
		return 0;
	}

	ret = rtk_pm_alarm_get_next_wakeup(data, &t);
	if (ret || t == 0) {
		dev_dbg(data->dev, "no wakeup time\n");
		return 0;
	}

	dev_info(data->dev, "set %lld sec\n", t);
	rtk_pm_wakeup_source_alarm_set(data->pm_dev, 1);
	rtk_pm_alarm_set_wakeup_time(data, t);
	return 0;
}

static int rtk_pm_alarm_suspend(struct device *dev)
{
	struct rtk_pm_alarm_data *data = dev_get_drvdata(dev);

	return rtk_pm_alarm_setup(data, data->suspend_enabled);
}

static int rtk_pm_alarm_resume(struct device *dev)
{
	struct rtk_pm_alarm_data *data = dev_get_drvdata(dev);
	time64_t t;
	uint32_t p;

	data->unconsumed_overhead_ms += data->additional_overhead_ms;
	p = data->unconsumed_overhead_ms / 1000;
	data->unconsumed_overhead_ms %= 1000;

	t = rtk_pm_alarm_get_advanced_time(data);

	dev_info(dev, "advance rtc %lld+%d sec\n", t, p);
	rtk_pm_alarm_advance_rtc_time(data, t + p);

	rtk_pm_alarm_set_wakeup_time(data, ~0);

	return 0;
}

static const struct dev_pm_ops rtk_pm_alarm_dev_pm_ops = {
	.suspend_late = rtk_pm_alarm_suspend,
	.resume_early = rtk_pm_alarm_resume,
};

static int rtk_pm_alarm_get_pm_dev(struct rtk_pm_alarm_data  *data)
{
	struct device_node *np;
	struct platform_device *pdev;

	np = of_parse_phandle(data->dev->of_node, "realtek,pm-device", 0);
	if (!np)
		return -EINVAL;

	pdev = of_find_device_by_node(np);
	of_node_put(np);
	if (!pdev)
		return -ENODEV;

	data->pm_dev = platform_get_drvdata(pdev);
	if (!data->pm_dev)
		return -EPROBE_DEFER;

	return 0;
}

static int rtk_pm_alarm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rtk_pm_alarm_data  *data;
	struct resource *res;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (IS_ERR(res))
		return PTR_ERR(res);

	data->base = devm_ioremap_resource(&pdev->dev, res);
	if (!data->base)
		return -ENOMEM;

	data->dev = dev;
	ret = rtk_pm_alarm_get_pm_dev(data);
	if (ret) {
		if (ret == -EPROBE_DEFER)
			dev_dbg(dev, "pm_dev not ready, retry\n");
		else
			dev_err(dev, "failed to get pm_dev: %d\n", ret);
		return ret;
	}

	data->rtc = rtc_class_open("rtc0");
	if (!data->rtc)
		return -EPROBE_DEFER;

	of_property_read_u32(dev->of_node, "additional-overhead-ms",
		&data->additional_overhead_ms);

	data->suspend_enabled = !of_property_read_bool(dev->of_node, "suspend-disabled");
	data->shutdown_enabled = of_property_read_bool(dev->of_node, "shutdown-enabled");

	platform_set_drvdata(pdev, data);

	return 0;
}

static int rtk_pm_alarm_remove(struct platform_device *pdev)
{
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static void rtk_pm_alarm_shutdown(struct platform_device *pdev)
{
	struct rtk_pm_alarm_data *data = platform_get_drvdata(pdev);

	rtk_pm_alarm_setup(data, data->shutdown_enabled);
}

static const struct of_device_id rtk_pm_alarm_match[] = {
	{ .compatible = "realtek,pm-alarm", },
	{}
};

static struct platform_driver rtk_pm_alarm_driver = {
	.probe    = rtk_pm_alarm_probe,
	.remove   = rtk_pm_alarm_remove,
	.shutdown = rtk_pm_alarm_shutdown,
	.driver = {
		.owner          = THIS_MODULE,
		.name           = "rtk-pm-alarm",
		.of_match_table = of_match_ptr(rtk_pm_alarm_match),
		.pm             = &rtk_pm_alarm_dev_pm_ops,
	},
};
module_platform_driver(rtk_pm_alarm_driver);

MODULE_DESCRIPTION("Realtek PM Alarm driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:rtk-pm-alarm");
MODULE_AUTHOR("Cheng-Yu Lee <cylee12@realtek.com>");
