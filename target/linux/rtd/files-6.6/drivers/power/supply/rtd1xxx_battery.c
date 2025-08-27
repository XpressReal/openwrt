// SPDX-License-Identifier: GPL-2.0
/*
 * Battery driver for Realtek DHC SoCs
 *
 * Copyright (c) 2021 Realtek Semiconductor Corp.
 */

#include <linux/module.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>

#define DEFAULT_VOLTAGE_UV 5000000

struct rtd1xxx_battery_data {
	struct power_supply *battery;
	int voltage_max;
	int voltage_min;
	int voltage_now;
};

static int rtd1xxx_battery_update_voltage(struct rtd1xxx_battery_data *data, int voltage_uv)
{
	if (data->voltage_now == voltage_uv)
		return 0;

	data->voltage_now = voltage_uv;
	if (voltage_uv > data->voltage_max)
		data->voltage_max = voltage_uv;
	if (voltage_uv < data->voltage_min)
		data->voltage_min = voltage_uv;

	power_supply_changed(data->battery);
	return 0;
}

static int rtd1xxx_bat_set_property(struct power_supply *psy,
				    enum power_supply_property psp,
				    const union power_supply_propval *val)
{
	struct rtd1xxx_battery_data *data = power_supply_get_drvdata(psy);
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = rtd1xxx_battery_update_voltage(data, val->intval);
		break;

	default:
		ret = -EINVAL;
	}
	return ret;
}

static int rtd1xxx_bat_get_property(struct power_supply *psy,
				    enum power_supply_property psp,
				    union power_supply_propval *val)
{
	struct rtd1xxx_battery_data *data = power_supply_get_drvdata(psy);
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = POWER_SUPPLY_STATUS_CHARGING;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		val->intval = 9;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		val->intval = DEFAULT_VOLTAGE_UV;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val->intval = data->voltage_max;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
		val->intval = data->voltage_min;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = data->voltage_now;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = 400;
		break;
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		val->intval = 400;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		val->intval = 10000000;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		val->intval = 4000000; //uAh
		break;
	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
		val->intval = 4000000;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = 100;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = 310; //0.1°C
		break;
	case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW:
		val->intval = 600;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static enum power_supply_property rtd1xxx_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MIN,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
};

static const struct power_supply_desc battery_desc = {
	.name = "rtd1xxx-battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = rtd1xxx_battery_props,
	.num_properties = ARRAY_SIZE(rtd1xxx_battery_props),
	.get_property = rtd1xxx_bat_get_property,
	.set_property = rtd1xxx_bat_set_property,
};

static int rtd1xxx_battery_probe(struct platform_device *pdev)
{
	struct power_supply_config cfg = {};
	struct rtd1xxx_battery_data *data;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->voltage_max = DEFAULT_VOLTAGE_UV;
	data->voltage_min = DEFAULT_VOLTAGE_UV;
	data->voltage_now = DEFAULT_VOLTAGE_UV;
	platform_set_drvdata(pdev, data);

	cfg.drv_data = data;
	cfg.of_node = pdev->dev.of_node;

	data->battery = devm_power_supply_register_no_ws(&pdev->dev, &battery_desc, &cfg);
	return PTR_ERR_OR_ZERO(data->battery);
}

static const struct of_device_id rtd1xxx_battery_id_table[] = {
	{ .compatible = "realtek,rtd1xxx-battery", },
	{}
};
MODULE_DEVICE_TABLE(of, rtd1xxx_battery_id_table);

static struct platform_driver rtd1xxx_battery_driver = {
	.probe = rtd1xxx_battery_probe,
	.driver = {
		.owner          = THIS_MODULE,
		.name           = "rtd1xxx-battery",
		.of_match_table = of_match_ptr(rtd1xxx_battery_id_table),
	},
};
module_platform_driver(rtd1xxx_battery_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Battery power supply driver for Realtek DHC SoCs");
MODULE_ALIAS("platform:rtd1xxx-battery");
