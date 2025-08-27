// SPDX-License-Identifier: GPL-2.0-only
/*
 * rtk_wdt.c - Realtek watchdog driver
 * Copyright (c) 2018-2022 Realtek Semiconductor Corp.
 */

#include <linux/io.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>
#include <linux/interrupt.h>
#include <linux/mfd/syscon.h>
#include <linux/reboot.h>

#define RTK_WDT_DEFAULT_TICKS_PER_SEC    (27000000)
#define RTK_WDT_DEFAULT_TIMEOUT          (20)
#define RTK_WDT_DEFAULT_PRETIMEOUT       (8)
#define RTK_WDT_DEFAULT_OV_RSTB_TIME_MS  (1000)
#define RTK_WDT_OV_RSTB_OE_INVALID       (-1)

#define WDT_OFFSET_TCWCR		0x0
#define WDT_OFFSET_TCWTR		0x4
#define WDT_OFFSET_TCWNMI		0x8
#define WDT_OFFSET_TCWOV		0xc

#define WDT_OVRSTB_OFFSET_CNT           0x0
#define WDT_OVRSTB_OFFSET_PAD           0x4

struct rtk_wdt {
	struct device *dev;
	struct watchdog_device wdd;
	void __iomem *wdt_base;
	int irq;

	void __iomem *ov_rstb_base;
	struct notifier_block reboot_nb;
	int ov_rstb_oe;
	int ov_rstb_oe_init;
	int ov_rstb_time_ms;
	int ignore_restart;
};

static inline int rtk_wdt_ov_rstb_oe_vaild(int val)
{
	return val == 1 || val == 0;
}

static inline int rtk_wdt_ov_rstb_msecs_to_ticks(unsigned int time_ms)
{
	return time_ms * (RTK_WDT_DEFAULT_TICKS_PER_SEC / 1000) - 4;
}

static inline int rtk_wdt_secs_to_ticks(unsigned int sec)
{
	return sec * RTK_WDT_DEFAULT_TICKS_PER_SEC;
}

static void rtk_wdt_enable(struct rtk_wdt *wdt)
{
	u32 val = readl(wdt->wdt_base + WDT_OFFSET_TCWCR);

	val &= ~0xff;
	val |= 0xff;
	writel(val, wdt->wdt_base + WDT_OFFSET_TCWCR);
}

static void rtk_wdt_disable(struct rtk_wdt *wdt)
{
	u32 val = readl(wdt->wdt_base + WDT_OFFSET_TCWCR);

	val &= ~0xff;
	val |= 0xa5;
	writel(val, wdt->wdt_base + WDT_OFFSET_TCWCR);
}

static int rtk_wdt_is_running(struct rtk_wdt *wdt)
{
	u32 val = readl(wdt->wdt_base + WDT_OFFSET_TCWCR);

	return (val & 0xff) == 0xff;
}

static void rtk_wdt_enable_int(struct rtk_wdt *wdt)
{
	u32 val = readl(wdt->wdt_base + WDT_OFFSET_TCWCR);

	val &= ~0x80000000;
	val |= 0x80000000;
	writel(val, wdt->wdt_base + WDT_OFFSET_TCWCR);
}

static void rtk_wdt_disable_int(struct rtk_wdt *wdt)
{
	u32 val = readl(wdt->wdt_base + WDT_OFFSET_TCWCR);

	val &= ~0x80000000;
	writel(val, wdt->wdt_base + WDT_OFFSET_TCWCR);
}

static void rtk_wdt_clear_cnt(struct rtk_wdt *wdt)
{
	writel(0x1, wdt->wdt_base + WDT_OFFSET_TCWTR);
}

static void rtk_wdt_set_nmi(struct rtk_wdt *wdt, u32 val)
{
	writel(val, wdt->wdt_base + WDT_OFFSET_TCWNMI);
}

static void rtk_wdt_set_ov(struct rtk_wdt *wdt, u32 val)
{
	writel(val, wdt->wdt_base + WDT_OFFSET_TCWOV);
}

static void rtk_wdt_set_ov_rstb_oe(struct rtk_wdt *wdt, int oe)
{
	u32 val;

	val = readl(wdt->ov_rstb_base + WDT_OVRSTB_OFFSET_PAD);
	val &= ~0x1;
	val |= oe & 0x1;
	writel(val, wdt->ov_rstb_base + WDT_OVRSTB_OFFSET_PAD);
}

static void rtk_wdt_set_ov_rstb_cnt(struct rtk_wdt *wdt, u32 cnt)
{
	writel(cnt, wdt->ov_rstb_base + WDT_OVRSTB_OFFSET_CNT);
}

static int __rtk_wdt_update(struct watchdog_device *wdd)
{
	struct rtk_wdt *wdt = watchdog_get_drvdata(wdd);
	unsigned int delta;

	rtk_wdt_disable(wdt);
	rtk_wdt_disable_int(wdt);

	rtk_wdt_set_ov(wdt, rtk_wdt_secs_to_ticks(wdd->timeout));
	rtk_wdt_enable(wdt);
	rtk_wdt_clear_cnt(wdt);

	if (!wdd->pretimeout || wdd->pretimeout > wdd->timeout)
		delta = 0;
	else
		delta = wdd->timeout - wdd->pretimeout;

	rtk_wdt_set_nmi(wdt, rtk_wdt_secs_to_ticks(delta));
	if (delta)
		rtk_wdt_enable_int(wdt);
	return 0;
}

static int rtk_wdt_start(struct watchdog_device *wdd)
{
	dev_dbg(wdd->parent, "%s\n", __func__);
	return __rtk_wdt_update(wdd);
}

static int rtk_wdt_stop(struct watchdog_device *wdd)
{
	struct rtk_wdt *wdt = watchdog_get_drvdata(wdd);

	dev_dbg(wdd->parent, "%s\n", __func__);
	rtk_wdt_disable(wdt);
	rtk_wdt_disable_int(wdt);
	return 0;
}

static int rtk_wdt_ping(struct watchdog_device *wdd)
{
	struct rtk_wdt *wdt = watchdog_get_drvdata(wdd);

	dev_dbg(wdd->parent, "%s\n", __func__);
	rtk_wdt_clear_cnt(wdt);
	return 0;
}

static int rtk_wdt_set_timeout(struct watchdog_device *wdd, unsigned int timeout)
{
	dev_dbg(wdd->parent, "%s: timeout=%u\n", __func__, timeout);
	wdd->timeout = timeout;
	return __rtk_wdt_update(wdd);
}

static int rtk_wdt_set_pretimeout(struct watchdog_device *wdd,
				  unsigned int timeout)
{
	dev_dbg(wdd->parent, "%s: pretimeout=%u\n", __func__, timeout);
	wdd->pretimeout = timeout;
	return __rtk_wdt_update(wdd);
}

static int rtk_wdt_restart(struct watchdog_device *wdd, unsigned long action,
			   void *data)
{
	struct rtk_wdt *wdt = watchdog_get_drvdata(wdd);

	if (wdt->ignore_restart) {
		dev_info(wdd->parent, "Ignore restart\n");
		return NOTIFY_DONE;
	}

	dev_emerg(wdd->parent, "Restarting...\n");

	rtk_wdt_disable(wdt);
	rtk_wdt_disable_int(wdt);
	rtk_wdt_clear_cnt(wdt);

	rtk_wdt_set_ov(wdt, 0x00800000);
	rtk_wdt_enable(wdt);

	mdelay(10000);
	dev_emerg(wdd->parent, "Unable to restart system\n");

	return NOTIFY_OK;
}

static irqreturn_t rtk_wdt_irq_handler(int irq, void *devid)
{
	struct rtk_wdt *wdt = devid;

	watchdog_notify_pretimeout(&wdt->wdd);
	return IRQ_HANDLED;
}

static void rtk_wdt_shutdown(struct platform_device *pdev)
{
	struct rtk_wdt *wdt = platform_get_drvdata(pdev);
	struct device *dev = wdt->wdd.parent;

	dev_info(dev, "%s\n", __func__);
	rtk_wdt_stop(&wdt->wdd);
}

static const struct watchdog_info rtk_wdt_info_pretimeout = {
	.identity = "Realtek watchdog",
	.options = WDIOF_KEEPALIVEPING | WDIOF_SETTIMEOUT | WDIOF_MAGICCLOSE |
		   WDIOF_PRETIMEOUT,
};

static const struct watchdog_info rtk_wdt_info = {
	.identity = "Realtek watchdog",
	.options = WDIOF_KEEPALIVEPING | WDIOF_SETTIMEOUT | WDIOF_MAGICCLOSE,
};

static const struct watchdog_ops rtk_wdt_ops = {
	.owner          = THIS_MODULE,
	.start          = rtk_wdt_start,
	.stop           = rtk_wdt_stop,
	.ping           = rtk_wdt_ping,
	.set_timeout    = rtk_wdt_set_timeout,
	.set_pretimeout = rtk_wdt_set_pretimeout,
	.restart        = rtk_wdt_restart,
};

static int rtk_wdt_reboot_notify(struct notifier_block *nb, unsigned long mode, void *cmd)
{
	struct rtk_wdt *wdt = container_of(nb, struct rtk_wdt, reboot_nb);

	if (!rtk_wdt_ov_rstb_oe_vaild(wdt->ov_rstb_oe))
		return NOTIFY_DONE;

	dev_info(wdt->dev, "set rstb-oe to %d\n", wdt->ov_rstb_oe);
	rtk_wdt_set_ov_rstb_oe(wdt, wdt->ov_rstb_oe);
	dev_info(wdt->dev, "set rstb-time %dms\n", wdt->ov_rstb_time_ms);
	rtk_wdt_set_ov_rstb_cnt(wdt, rtk_wdt_ov_rstb_msecs_to_ticks(wdt->ov_rstb_time_ms));
	return NOTIFY_OK;
}

static void rtk_wdt_fini_ov_rstb(void *data)
{
	struct rtk_wdt *wdt = data;

	unregister_reboot_notifier(&wdt->reboot_nb);
}

static int rtk_wdt_init_ov_rstb(struct rtk_wdt *wdt)
{
	struct device_node *np = wdt->dev->of_node;

	wdt->ov_rstb_oe_init = RTK_WDT_OV_RSTB_OE_INVALID;
	of_property_read_u32(np, "realtek,ov-rstb-oe-init", &wdt->ov_rstb_oe_init);

	wdt->ov_rstb_oe = RTK_WDT_OV_RSTB_OE_INVALID;
	of_property_read_u32(np, "realtek,ov-rstb-oe", &wdt->ov_rstb_oe);

	wdt->ov_rstb_time_ms = RTK_WDT_DEFAULT_OV_RSTB_TIME_MS;
	of_property_read_u32(np, "realtek,ov-rstb-time-ms", &wdt->ov_rstb_time_ms);

	wdt->reboot_nb.notifier_call = rtk_wdt_reboot_notify;
	register_reboot_notifier(&wdt->reboot_nb);

	return devm_add_action_or_reset(wdt->dev, rtk_wdt_fini_ov_rstb, wdt);
}

static int rtk_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rtk_wdt *wdt;
	struct resource *res;
	int ret = 0;

	wdt = devm_kzalloc(dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;

	wdt->dev = dev;
	wdt->wdt_base = devm_ioremap(dev, res->start, resource_size(res));
	if (!wdt->wdt_base)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (res) {
		wdt->ov_rstb_base = devm_ioremap(dev, res->start, resource_size(res));
		if (!wdt->ov_rstb_base) {
			dev_err(dev, "failed to map rstb\n");
			return -ENOMEM;
		}

		ret = rtk_wdt_init_ov_rstb(wdt);
		if (ret)
			dev_warn(dev, "failed to init rstb: %d\n", ret);

		if (rtk_wdt_ov_rstb_oe_vaild(wdt->ov_rstb_oe_init))
			rtk_wdt_set_ov_rstb_oe(wdt, wdt->ov_rstb_oe_init);
	}

	wdt->ignore_restart = of_property_read_bool(dev->of_node, "realtek,ignore-restart");

	ret = platform_get_irq_optional(pdev, 0);
	if (ret < 0 && ret != -ENXIO) {
		if (ret == -EPROBE_DEFER)
			dev_dbg(dev, "irq is not ready, retry\n");
		else
			dev_err(dev, "failed to get irq: %d\n", ret);
		return ret;
	} else if (ret != -ENXIO) {
		wdt->irq = ret;
		ret = devm_request_irq(dev, wdt->irq, rtk_wdt_irq_handler, 0,
			       dev_name(dev), wdt);
		if (ret) {
			dev_err(dev, "failed to request irq: %d\n", ret);
			return ret;
		}
	}

	watchdog_set_drvdata(&wdt->wdd, wdt);
	wdt->wdd.parent      = dev;
	wdt->wdd.info        = wdt->irq ? &rtk_wdt_info_pretimeout : &rtk_wdt_info;
	wdt->wdd.ops         = &rtk_wdt_ops;
	wdt->wdd.min_timeout = 1;
	wdt->wdd.max_timeout = UINT_MAX / RTK_WDT_DEFAULT_TICKS_PER_SEC;
	wdt->wdd.timeout     = RTK_WDT_DEFAULT_TIMEOUT;
	wdt->wdd.pretimeout  = RTK_WDT_DEFAULT_PRETIMEOUT;
	watchdog_init_timeout(&wdt->wdd, 0, dev);
	watchdog_set_restart_priority(&wdt->wdd, 0);


	if (rtk_wdt_is_running(wdt)) {
		dev_info(dev, "watchdog already running\n");
		set_bit(WDOG_HW_RUNNING, &wdt->wdd.status);
	}

	ret = watchdog_register_device(&wdt->wdd);
	if (ret) {
		dev_err(dev, "cannot register watchdog device: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, wdt);

	return 0;
}

static int rtk_wdt_remove(struct platform_device *pdev)
{
	struct rtk_wdt *wdt = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);
	watchdog_unregister_device(&wdt->wdd);
	return 0;
}

static int __maybe_unused rtk_wdt_suspend(struct device *dev)
{
	struct rtk_wdt *wdt = dev_get_drvdata(dev);

	if (watchdog_active(&wdt->wdd))
		rtk_wdt_stop(&wdt->wdd);

	return 0;
}

static int __maybe_unused rtk_wdt_resume(struct device *dev)
{
	struct rtk_wdt *wdt = dev_get_drvdata(dev);

	if (watchdog_active(&wdt->wdd))
		rtk_wdt_start(&wdt->wdd);

	return 0;
}

static SIMPLE_DEV_PM_OPS(rtk_wdt_pm_ops, rtk_wdt_suspend, rtk_wdt_resume);

static const struct of_device_id rtk_wdt_of_table[] = {
	{ .compatible = "realtek,watchdog", },
	{}
};
MODULE_DEVICE_TABLE(of, rtk_wdt_of_table);

static struct platform_driver rtk_watchdog_driver = {
	.probe    = rtk_wdt_probe,
	.remove   = rtk_wdt_remove,
	.shutdown = rtk_wdt_shutdown,
	.driver = {
		.name = "rtk-wdt",
		.of_match_table = rtk_wdt_of_table,
		.pm = &rtk_wdt_pm_ops,
	},
};
module_platform_driver(rtk_watchdog_driver);

MODULE_DESCRIPTION("Realtek Watchdog Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Cheng-Yu Lee <cylee12@realtek.com>");
