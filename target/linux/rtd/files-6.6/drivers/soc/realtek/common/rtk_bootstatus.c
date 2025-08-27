// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021-2022 Realtek Semiconductor Corp.
 */

#include <linux/init.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

enum {
	REBOOT_REASON_INVALID = 0,
	REBOOT_REASON_SOFTWARE,
	REBOOT_REASON_WATCHDOG,
	REBOOT_REASON_SUSPEND_ABNORMAL,
	REBOOT_REASON_SHUTDOWN_ABNORMAL,
};

static const char * const reboot_reason_strings[] = {
	"invalid", "software", "watchdog", "suspend_error", "shutdown_error",
};

enum {
	BOOT_TYPE_EMMC,
	BOOT_TYPE_USB_DEVICE,
	BOOT_TYPE_NOR,
	BOOT_TYPE_NAND,
	BOOT_TYPE_NAND_SERIAL,
	BOOT_TYPE_NAND_SERIAL_3V3,
	BOOT_TYPE_NAND_SERIAL_1V8,
	BOOT_TYPE_NAND_PARALLEL,
};

static const char * const boot_type_strings[] = {
	[BOOT_TYPE_EMMC]            = "emmc",
	[BOOT_TYPE_USB_DEVICE]      = "usb-device",
	[BOOT_TYPE_NOR]             = "nor",
	[BOOT_TYPE_NAND]            = "nand",
	[BOOT_TYPE_NAND_SERIAL]     = "nand-serial",
	[BOOT_TYPE_NAND_SERIAL_3V3] = "nand-serial-3v3",
	[BOOT_TYPE_NAND_SERIAL_1V8] = "nand-serial-1v8",
	[BOOT_TYPE_NAND_PARALLEL]   = "nand-parellel",
};

struct bootstatus_plat_desc {
	const char *(*boot_type_string)(int opt);
};

struct bootstatus_data {
	const struct bootstatus_plat_desc *desc;

	unsigned long reboot_reason;

	struct regmap *regmap_boot_type;
	int offset_boot_type;

	/* sw_cold_boot_counter */
	struct regmap *regmap_scb_cnt;
	int offset_scb_cnt;
};

static const char *bootstatus_get_boot_type(struct bootstatus_data *data)
{
	unsigned int val;

	regmap_read(data->regmap_boot_type, data->offset_boot_type, &val);
	return data->desc->boot_type_string(val);
}

static const unsigned int rtd1619_boot_type_map[] = {
	BOOT_TYPE_NAND, BOOT_TYPE_NOR, BOOT_TYPE_USB_DEVICE, BOOT_TYPE_EMMC
};

static const char *rtd1619_boot_type_string(int opt)
{
	int sel = (opt >> 29) & 0x3;

	return boot_type_strings[rtd1619_boot_type_map[sel]];
}

static const struct bootstatus_plat_desc rtd1619_desc = {
	.boot_type_string = rtd1619_boot_type_string,
};

static const unsigned int rtd1319_boot_type_map[] = {
	BOOT_TYPE_NAND_SERIAL, BOOT_TYPE_NAND_PARALLEL,
	BOOT_TYPE_NOR,         BOOT_TYPE_NOR,
	BOOT_TYPE_USB_DEVICE,  BOOT_TYPE_USB_DEVICE,
	BOOT_TYPE_EMMC,        BOOT_TYPE_EMMC
};

static const char *rtd1319_boot_type_string(int opt)
{
	int sel = (opt >> 28) & 0x7;

	return boot_type_strings[rtd1319_boot_type_map[sel]];
}

static const struct bootstatus_plat_desc rtd1319_desc = {
	.boot_type_string = rtd1319_boot_type_string,
};

static const unsigned int rtd1619b_boot_type_map[] = {
	BOOT_TYPE_NAND_SERIAL_1V8, BOOT_TYPE_NAND_SERIAL_3V3,
	BOOT_TYPE_NOR,             BOOT_TYPE_NAND_PARALLEL,
	BOOT_TYPE_USB_DEVICE,      BOOT_TYPE_USB_DEVICE,
	BOOT_TYPE_EMMC,            BOOT_TYPE_EMMC
};

static const char *rtd1619b_boot_type_string(int opt)
{
	int sel = (opt >> 28) & 0x7;

	return boot_type_strings[rtd1619b_boot_type_map[sel]];
}

static const struct bootstatus_plat_desc rtd1619b_desc = {
	.boot_type_string = rtd1619b_boot_type_string,
};

static const char * const param_reboot_reason_strings[] = {
	[REBOOT_REASON_INVALID] = "hardware",
	[REBOOT_REASON_SUSPEND_ABNORMAL] = "str_warm",
	[REBOOT_REASON_SHUTDOWN_ABNORMAL] = "str_cold",
	[REBOOT_REASON_SOFTWARE] = "software",
	[REBOOT_REASON_WATCHDOG] = "watchdog",
};

static int parse_reboot_reason_str(const char *str, unsigned long *out)
{
	int i;

	*out = REBOOT_REASON_INVALID;
	if (!str)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(param_reboot_reason_strings); i++)
		if (strcmp(str, param_reboot_reason_strings[i]) == 0) {
			*out = i;
			return 0;
		}

	pr_err("%s: invalid wakeupreason value \"%s\"\n", __func__, str);
	return -EINVAL;
}

static long reboot_reason;

__attribute__((unused)) static int reboot_reason_config(char *str)
{
	return parse_reboot_reason_str(str, &reboot_reason);
}
__setup("wakeupreason=", reboot_reason_config);

static int param_set_reboot_reason(const char *str, const struct kernel_param *kp)
{
	return parse_reboot_reason_str(str, &reboot_reason);
}

static int param_get_reboot_reason(char *str, const struct kernel_param *kp)
{
	if (reboot_reason < 0 || reboot_reason >= ARRAY_SIZE(param_reboot_reason_strings))
		return -EINVAL;

	return scnprintf(str, PAGE_SIZE, "%s\n", param_reboot_reason_strings[reboot_reason]);
}

static const struct kernel_param_ops reboot_reason_param_ops = {
	.set = param_set_reboot_reason,
	.get = param_get_reboot_reason,
};

device_param_cb(reboot_reason, &reboot_reason_param_ops, NULL, 0444);
MODULE_PARM_DESC(reboot_reason, "Reboot Reason");

static ssize_t reboot_reason_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct bootstatus_data *data = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%s\n", reboot_reason_strings[data->reboot_reason]);
}
static DEVICE_ATTR_RO(reboot_reason);

static ssize_t boot_type_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct bootstatus_data *data = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%s\n", bootstatus_get_boot_type(data));
}
static DEVICE_ATTR_RO(boot_type);

static ssize_t sw_cold_boot_counter_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct bootstatus_data *data = dev_get_drvdata(dev);
	unsigned int val;

	regmap_read(data->regmap_scb_cnt, data->offset_scb_cnt, &val);
	return sprintf(buf, "%u\n", val);
}

static ssize_t sw_cold_boot_counter_store(struct device *dev, struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct bootstatus_data *data = dev_get_drvdata(dev);
	int ret;
	unsigned int val;

	ret = kstrtouint(buf, 0, &val);
	if (!ret)
		regmap_write(data->regmap_scb_cnt, data->offset_scb_cnt, val);

	return ret ?: count;
}
static DEVICE_ATTR_RW(sw_cold_boot_counter);

static struct attribute *rtk_bootstatus_attrs[] = {
	&dev_attr_reboot_reason.attr,
	&dev_attr_boot_type.attr,
	&dev_attr_sw_cold_boot_counter.attr,
	NULL,
};

static umode_t rtk_bootstatus_attr_is_visible(struct kobject *kobj, struct attribute *attr, int unused)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct bootstatus_data *data = dev_get_drvdata(dev);

	if (attr == &dev_attr_boot_type.attr && !data->regmap_boot_type)
		return 0;

	if (attr == &dev_attr_sw_cold_boot_counter.attr && !data->regmap_scb_cnt)
		return 0;

	return attr->mode;
}

static struct attribute_group rtk_bootstatus_attr_group = {
	.attrs      = rtk_bootstatus_attrs,
	.is_visible = rtk_bootstatus_attr_is_visible,
};

static int rtk_bootstatus_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct bootstatus_data *data;
	int ret = 0;
	struct regmap *regmap;
	int offset;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->desc = of_device_get_match_data(dev);
	if (!data->desc)
		return -EINVAL;

	regmap = syscon_regmap_lookup_by_phandle_args(np, "realtek,boot-type",
		1, &offset);
	if (!IS_ERR(regmap)) {
		dev_info(dev, "boot-type configured\n");
		data->regmap_boot_type = regmap;
		data->offset_boot_type = offset;
	}

	regmap = syscon_regmap_lookup_by_phandle_args(np, "realtek,sw-cold-boot-count",
		1, &offset);
	if (!IS_ERR(regmap)) {
		dev_info(dev, "sw-cold-boot-count configured\n");
		data->regmap_scb_cnt = regmap;
		data->offset_scb_cnt = offset;
	}

	data->reboot_reason = reboot_reason;
	platform_set_drvdata(pdev, data);

	ret = sysfs_create_group(&dev->kobj, &rtk_bootstatus_attr_group);
	if (ret) {
		dev_err(dev, "error %pe: failed to create sysfs", ERR_PTR(ret));
		return ret;
	}

	ret = sysfs_create_link(kernel_kobj, &dev->kobj, "bootstatus");
	if (ret)
		dev_warn(dev, "failed to create bootstatus link");

	return 0;
}

static int rtk_bootstatus_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	sysfs_remove_group(&dev->kobj, &rtk_bootstatus_attr_group);
	return 0;
}

static const struct of_device_id rtk_bootstatus_match[] = {
	{ .compatible = "realtek,rtd1619-bootstatus", .data = &rtd1619_desc, },
	{ .compatible = "realtek,rtd1319-bootstatus", .data = &rtd1319_desc, },
	{ .compatible = "realtek,rtd1619b-bootstatus", .data = &rtd1619b_desc, },
	{}
};

static struct platform_driver rtk_bootstatus_driver = {
	.probe    = rtk_bootstatus_probe,
	.remove   = rtk_bootstatus_remove,
	.driver = {
		.owner          = THIS_MODULE,
		.name           = "rtk-bootstatus",
		.of_match_table = of_match_ptr(rtk_bootstatus_match),
	},
};
module_platform_driver(rtk_bootstatus_driver);

MODULE_DESCRIPTION("Realtek Bootstatus Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Cheng-Yu Lee <cylee12@realtek.com>");
