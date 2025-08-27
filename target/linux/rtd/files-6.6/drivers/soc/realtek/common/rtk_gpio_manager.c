// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * Realtek DHC gpio default init driver
 *
 * Copyright (c) 2019 Realtek Semiconductor Corp.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/gpio/consumer.h>
#include <soc/realtek/rtk_pm.h>



struct rtd_gpio_manager_state {
	struct gpio_descs *gpio;
	int *values;
};

int rtd_gpio_set_default(struct device *dev)
{
	struct gpio_descs *rtd_gpios;
	int num = 0;
	int value = 0;
	int i;
	int ret = 0;

	rtd_gpios = gpiod_get_array(dev, "default", GPIOD_ASIS);
	if (IS_ERR(rtd_gpios)) {
		if (PTR_ERR(rtd_gpios) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		else {
			dev_info(dev, "No default gpios need to be set\n");
			goto out;
		}
	} else
		dev_info(dev, "set %d gpios to default value\n", rtd_gpios->ndescs);


	num = of_property_count_u32_elems(dev->of_node, "default-gpios-state");
	if (num < 0 || num != rtd_gpios->ndescs) {
		dev_err(dev, "number of gpio-default is not match to number of gpios\n");
		goto free_resources;
	}
	for (i = 0; i < num; i++) {
		of_property_read_u32_index(dev->of_node, "default-gpios-state", i, &value);
		switch (value) {
		case 0:
			ret = gpiod_direction_output(rtd_gpios->desc[i], 0);
			if (ret)
				dev_err(dev, "cannot set gpio %d", desc_to_gpio(rtd_gpios->desc[i]));
			break;
		case 1:
			ret = gpiod_direction_output(rtd_gpios->desc[i], 1);
			if (ret)
				dev_err(dev, "cannot set gpio %d", desc_to_gpio(rtd_gpios->desc[i]));
			break;
		case 2:
			ret = gpiod_direction_input(rtd_gpios->desc[i]);
			if (ret)
				dev_err(dev, "cannot set gpio %d", desc_to_gpio(rtd_gpios->desc[i]));
			break;
		default:
			dev_err(dev, "default value is not support for gpio %d", desc_to_gpio(rtd_gpios->desc[i]));

		}
	}

free_resources:
	gpiod_put_array(rtd_gpios);

out:
	return 0;
}

void rtd_gpio_set_output_change(struct device *dev, int state)
{
	struct gpio_descs *rtd_gpios;
	int num = 0;
	int value = 0;
	int i;
	int ret = 0;

	rtd_gpios = gpiod_get_array(dev, "change", GPIOD_ASIS);
	if (IS_ERR(rtd_gpios)) {
		dev_info(dev, "No gpios need to be change state\n");
		goto out;
	} else
		dev_info(dev, "change %d gpios state\n", rtd_gpios->ndescs);


	num = of_property_count_u32_elems(dev->of_node, "change-gpios-value");
	if (num < 0 || num != rtd_gpios->ndescs) {
		dev_err(dev, "number of gpio-output-change-state is not match to number of gpios\n");
		goto free_resources;
	}
	for (i = 0; i < num; i++) {
		of_property_read_u32_index(dev->of_node, "change-gpios-value", i, &value);
		if (!state)
			value = !value;
		switch (value) {
		case 0:
			ret = gpiod_direction_output(rtd_gpios->desc[i], 0);
			if (ret)
				dev_err(dev, "cannot set gpio %d", desc_to_gpio(rtd_gpios->desc[i]));
			break;
		case 1:
			ret = gpiod_direction_output(rtd_gpios->desc[i], 1);
			if (ret)
				dev_err(dev, "cannot set gpio %d", desc_to_gpio(rtd_gpios->desc[i]));
			break;
		default:
			dev_err(dev, "not support for gpio %d", desc_to_gpio(rtd_gpios->desc[i]));

		}
	}

free_resources:
	gpiod_put_array(rtd_gpios);

out:
	return;
}

static int rtd_gpio_manager_probe(struct platform_device *pdev)
{
	int ret = 0;

	ret = rtd_gpio_set_default(&pdev->dev);
	rtd_gpio_set_output_change(&pdev->dev, 0);

	return ret;
}


#ifdef CONFIG_PM
static int rtk_gpio_manager_pm_suspend(struct device *dev)
{
	rtd_gpio_set_output_change(dev, 1);

	return 0;
}

static int rtk_gpio_manager_pm_resume(struct device *dev)
{
	if (rtk_pm_get_wakeup_reason() != ALARM_EVENT)
		rtd_gpio_set_output_change(dev, 0);

	return 0;
}
static const struct dev_pm_ops rtk_gpio_manager_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(rtk_gpio_manager_pm_suspend, rtk_gpio_manager_pm_resume)
};
#endif

static void rtk_gpio_manager_pm_shutdown(struct platform_device *pdev)
{
	rtd_gpio_set_output_change(&pdev->dev, 1);
}

static const struct of_device_id rtd_gpio_manager_of_matches[] = {
	{ .compatible = "realtek,gpio-manager" },
	{ /* Sentinel */ },
};


static struct platform_driver rtd_gpio_manager_driver = {
	.driver = {
		.name = "rtd-gpio-manager",
		.of_match_table = rtd_gpio_manager_of_matches,
#ifdef CONFIG_PM
		.pm = &rtk_gpio_manager_pm_ops,
#endif
	},
	.probe = rtd_gpio_manager_probe,
	.shutdown = rtk_gpio_manager_pm_shutdown,
};


static int rtd_gpio_manager(void)
{
	return platform_driver_register(&rtd_gpio_manager_driver);
}

postcore_initcall(rtd_gpio_manager);

MODULE_AUTHOR("TYChang <tychang@realtek.com>");
MODULE_DESCRIPTION("Realtek GPIO Manager driver");
MODULE_LICENSE("GPL v2");
