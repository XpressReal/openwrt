/*
 * Copyright (C) 2011 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rfkill.h>
#include <linux/gpio/consumer.h>
#include <linux/pinctrl/consumer.h>
#include <linux/ioport.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#define LSADC0_PAD0 0x900
#define ISO_PFUNC21 0x74

#define ISO_DBG_STATUS 0x190
#define ISO_PFUNC0 0x2c
#define ISO_PFUNC1 0x30
#define ISO_PFUNC6 0x3c
#define ISO_PFUNC7 0x40

enum voltage_config {
	DETECT = 0,
	FIX_1V8,
	FIX_3V3,
	DETECT_NONE,
};

static struct rfkill *bt_rfk;
static const char bt_name[] = "bluetooth";
static struct gpio_desc *bt_reset;

struct pinctrl *pinctrl;
struct pinctrl_state *pins_default;
struct pinctrl_state *pins_1v8;
struct pinctrl_state *pins_3v3;

static int bluetooth_set_power(void *data, bool blocked)
{
	int err = 0;

	pr_info("%s: block=%d\n", __func__, blocked);

	if (!blocked)
		err = gpiod_direction_output(bt_reset, 1);
	else
		err = gpiod_direction_output(bt_reset, 0);

	if (err)
		pr_err("%s set bt power fail\n", __func__);

	return 0;
}

static struct rfkill_ops rfkill_bluetooth_ops = {
	.set_block = bluetooth_set_power,
};

static int rfkill_gpio_init(struct device *dev)
{
	/*initial gpios*/
	/* get gpio number from device tree*/
	bt_reset = devm_gpiod_get(dev, "rfkill", GPIOD_OUT_LOW);
	if (IS_ERR(bt_reset)) {
		pr_err("[%s ] could not request gpio\n", __func__);
		return -1;
	}

	return 0;
}

static void rfkill_gpio_deinit(void)
{
	gpiod_put(bt_reset);
}

static int rfkill_bluetooth_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	bool default_state = true;
	struct regmap *iso_dbg;
	unsigned int val = 0x0;
	const u32 *prop;
        int size;
	int rc = 0;
	int mode = DETECT;

	pr_info("-->%s\n", __func__);

	rc = rfkill_gpio_init(dev);
	if (rc) {
		dev_err(dev, "rfkill_gpio_init fail\n");
		goto deferred;
	}

	pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR(pinctrl)) {
		dev_dbg(dev, "no pinctrl\n");
		mode = DETECT_NONE;
	} else {
	        pins_default = pinctrl_lookup_state(pinctrl, PINCTRL_STATE_DEFAULT);
		if (IS_ERR(pins_default)) {
			dev_warn(dev, "could not get default state\n");
			pins_default = NULL;
		}

		pins_3v3 = pinctrl_lookup_state(pinctrl, "voltage-3v3");
	        if (IS_ERR(pins_3v3)) {
			dev_warn(dev, "could not get state voltage-3v3\n");
			pins_3v3 = NULL;
		}

	        pins_1v8 = pinctrl_lookup_state(pinctrl, "voltage-1v8");
		if (IS_ERR(pins_1v8)) {
			dev_warn(dev, "could not get state voltage-1v8\n");
			pins_1v8 = NULL;
		}
	}

	prop = of_get_property(dev->of_node, "voltage-config", &size);
        if (prop)
                mode = of_read_number(prop, 1);

	if (mode == DETECT) {
		iso_dbg = syscon_regmap_lookup_by_phandle(
					node, "realtek,pinctrl");

		val = BIT(6);
		regmap_update_bits_base(iso_dbg, ISO_DBG_STATUS, val, val,
					NULL, false, true);
		mdelay(100);
		regmap_read(iso_dbg, ISO_DBG_STATUS, &val);

		if(val & BIT(3)) {	/* 3.3v */
			if (pins_3v3)
				pinctrl_select_state(pinctrl, pins_3v3);
		} else {			/* 1.8v */
			if (pins_1v8)
				pinctrl_select_state(pinctrl, pins_1v8);
		}
	}
	else if (mode == FIX_1V8) {
		if (pins_1v8)
			pinctrl_select_state(pinctrl, pins_1v8);
	}
	else if (mode == FIX_3V3) {
		if (pins_3v3)
			pinctrl_select_state(pinctrl, pins_3v3);
	}

	bt_rfk = rfkill_alloc(bt_name, &pdev->dev, RFKILL_TYPE_BLUETOOTH,
				&rfkill_bluetooth_ops, NULL);
	if (!bt_rfk) {
		rc = -ENOMEM;
		goto err_rfkill_alloc;
	}

	/* userspace cannot take exclusive control */
	rfkill_init_sw_state(bt_rfk, false);
	rc = rfkill_register(bt_rfk);
	if (rc)
		goto err_rfkill_reg;

	rfkill_set_sw_state(bt_rfk, true);
	bluetooth_set_power(NULL, default_state);

	pr_info("<--%s\n", __func__);
	return 0;

err_rfkill_reg:
	rfkill_destroy(bt_rfk);
err_rfkill_alloc:
	return rc;
deferred:
	return -EPROBE_DEFER;
}

static int rfkill_bluetooth_remove(struct platform_device *dev)
{
	pr_info("-->%s\n", __func__);
	rfkill_gpio_deinit();
	rfkill_unregister(bt_rfk);
	rfkill_destroy(bt_rfk);
	pr_info("<--%s\n", __func__);

	return 0;
}

static const struct of_device_id rtk_bt_ids[] = {
	{ .compatible = "realtek,rfkill" },
	{ /* Sentinel */ },
};

static struct platform_driver rfkill_bluetooth_driver = {
	.probe  = rfkill_bluetooth_probe,
	.remove = rfkill_bluetooth_remove,
	.driver = {
		.name = "rfkill",
		.owner = THIS_MODULE,
		.of_match_table = rtk_bt_ids,
	},
};

static int __init rfkill_bluetooth_init(void)
{
	pr_info("-->%s\n", __func__);
	return platform_driver_register(&rfkill_bluetooth_driver);
}

static void __exit rfkill_bluetooth_exit(void)
{
	pr_info("-->%s\n", __func__);
	platform_driver_unregister(&rfkill_bluetooth_driver);
}

late_initcall(rfkill_bluetooth_init);
module_exit(rfkill_bluetooth_exit);
MODULE_DESCRIPTION("bluetooth rfkill");
MODULE_AUTHOR("rs <wn@realsil.com.cn>");
MODULE_LICENSE("GPL");

