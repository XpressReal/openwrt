// SPDX-License-Identifier: GPL-2.0
/*
 * AC driver for Realtek DHC SoCs
 *
 * Copyright (c) 2021 Realtek Semiconductor Corp.
 */

#include <linux/module.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>

static unsigned int max_current;
module_param(max_current, uint, 0644);
MODULE_PARM_DESC(max_current, "Max AC current in mA");

static char adapter_quality[20];
module_param_string(adapter_quality, adapter_quality, sizeof(adapter_quality), 0644);
MODULE_PARM_DESC(adapter_quality, "Quality of the adapt");

struct rtd1xxx_ac_data {
	struct power_supply *psy_ac;
};

static ssize_t adapter_quality_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", adapter_quality);
}

static DEVICE_ATTR_RO(adapter_quality);

static struct attribute *rtd1xxx_ac_attrs[] = {
	&dev_attr_adapter_quality.attr,
        NULL,
};

ATTRIBUTE_GROUPS(rtd1xxx_ac);

static int rtd1xxx_ac_get_prop(struct power_supply *psy,
			       enum power_supply_property psp,
			       union power_supply_propval *val)
{
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = 1;
		break;

	case POWER_SUPPLY_PROP_CURRENT_MAX:
		if (max_current)
			val->intval = max_current * 1000; /* uA */
		else
			ret = -ENODATA;
		break;

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static enum power_supply_property rtd1xxx_ac_props[] = {
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_ONLINE,
};

static const struct power_supply_desc ac_desc = {
	.name = "rtd1xxx-ac",
	.type = POWER_SUPPLY_TYPE_MAINS,
	.properties = rtd1xxx_ac_props,
	.num_properties = ARRAY_SIZE(rtd1xxx_ac_props),
	.get_property = rtd1xxx_ac_get_prop,
};

static int rtd1xxx_ac_probe(struct platform_device *pdev)
{
	struct power_supply_config cfg = {};
	struct rtd1xxx_ac_data *data;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	platform_set_drvdata(pdev, data);

	cfg.drv_data = data;
	cfg.of_node = pdev->dev.of_node;
	cfg.attr_grp = rtd1xxx_ac_groups;

	data->psy_ac = devm_power_supply_register_no_ws(&pdev->dev, &ac_desc, &cfg);
	return PTR_ERR_OR_ZERO(data->psy_ac);
}

static const struct of_device_id rtd1xxx_ac_id_table[] = {
	{ .compatible = "realtek,rtd1xxx-ac", },
	{}
};
MODULE_DEVICE_TABLE(of, rtd1xxx_ac_id_table);

static struct platform_driver rtd1xxx_ac_driver = {
	.probe = rtd1xxx_ac_probe,
	.driver = {
		.owner          = THIS_MODULE,
		.name           = "rtd1xxx-ac",
		.of_match_table = of_match_ptr(rtd1xxx_ac_id_table),
	},
};
module_platform_driver(rtd1xxx_ac_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("AC power supply driver for Realtek DHC SoCs");
MODULE_ALIAS("platform:rtd1xxx-ac");
