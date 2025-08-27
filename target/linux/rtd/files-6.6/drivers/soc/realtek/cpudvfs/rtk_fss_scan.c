// SPDX-License-Identifier: GPL-2.0-only
/*
 * Realtek FSS Scan Driver
 *
 * Copyright (c) 2020-2021 Realtek Semiconductor Corp.
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/iopoll.h>
#include <linux/workqueue.h>
#include <soc/realtek/rtk_fss.h>

struct fss_scan_freq {
	int freq;
	int target_cdl0;
	int target_min_cdl1;
};

struct fss_scan_device {
	struct device             *dev;
	struct fss_control        *fss_ctl;
	struct clk                *clk;
	struct regulator          *supply;

	unsigned long             saved_freq;
	int                       saved_volt;

	int                       num_freqs;
	struct fss_scan_freq      *freqs;
	int                       scan_num;
	int                       volt_max;
	int                       volt_min;

	int                       *volts;

	struct work_struct        work;
	int                       freeze_task;
};

static int fss_scan_set_freq_volt(struct fss_scan_device *fdev, int freq, int volt)
{
	long old_freq = clk_get_rate(fdev->clk);
	int ret;

	/* always set the voltage */
	if (freq >= old_freq) {
		ret = regulator_set_voltage(fdev->supply, volt, volt);
		if (ret)
			return ret;
	}

	ret = clk_set_rate(fdev->clk, freq);
	if (ret)
		return ret;

	if (freq < old_freq) {
		ret = regulator_set_voltage(fdev->supply, volt, volt);
		if (ret)
			return ret;
	}
	return 0;
}

static void fss_scan_save_state(struct fss_scan_device *fdev)
{
	fdev->saved_volt = regulator_get_voltage(fdev->supply);
	fdev->saved_freq = clk_get_rate(fdev->clk);
}

static void fss_scan_restore_state(struct fss_scan_device *fdev)
{
	fss_scan_set_freq_volt(fdev, fdev->saved_freq, fdev->saved_volt);
}

static int cal_data_valid(unsigned int bitmap, unsigned int size, unsigned int val, unsigned int target)
{
	int i;
	unsigned int mask = (1 << size) - 1;

	for (i = ffs(bitmap); i ; i = ffs(bitmap)) {
		i -= 1;
		bitmap &= ~BIT(i);

		if (((val >> (i * size)) & mask) < target)
			return 0;
	}

	return 1;
}

static int cal_data_matched(unsigned int bitmap, unsigned int size, unsigned int val, unsigned int target)
{
	int i;
	unsigned int mask = (1 << size) - 1;

	for (i = ffs(bitmap); i ; i = ffs(bitmap)) {
		i -= 1;
		bitmap &= ~BIT(i);

		if (((val >> (i * size)) & mask) != target)
			return 0;
	}

	return 1;
}

struct fss_scan_info {
	const struct fss_scan_freq *fc;
	int volt;
	int cdl0_full_matched;
};

static int __cal_data_valid_v4(struct fss_scan_info *info, struct fss_calibration_data *data)
{
        const struct fss_scan_freq *fc = info->fc;
	struct fss_calibration_data_cell *fss = &data->fss, *dsu = &data->dsu;
        int ret;

        ret = cal_data_valid(fss->bitmap, fss->cdl0_size, fss->cdl0, fc->target_cdl0) &&
                cal_data_valid(dsu->bitmap, dsu->cdl0_size, dsu->cdl0, fc->target_cdl0);
        if (!ret)
                return ret;

        if (!info->cdl0_full_matched)
                info->cdl0_full_matched =
                        cal_data_matched(fss->bitmap, fss->cdl0_size, fss->cdl0, fc->target_cdl0) &&
                        cal_data_matched(dsu->bitmap, dsu->cdl0_size, dsu->cdl0, fc->target_cdl0);

        if (!info->cdl0_full_matched)
                return ret;

        return cal_data_valid(fss->bitmap, fss->cdl1_size, fss->min_cdl1, fc->target_min_cdl1) &&
                cal_data_valid(dsu->bitmap, fss->cdl1_size, dsu->min_cdl1, fc->target_min_cdl1);
}


static int fss_scan_calibrate_and_check(struct fss_scan_device *fdev, struct fss_scan_info *info)
{
        const struct fss_scan_freq *fc = info->fc;
	struct fss_calibration_data data = { 0 };
	struct fss_calibration_data_cell *fss = &data.fss, *dsu = &data.dsu;
	int ret;

	ret = fss_control_get_calibration_data(fdev->fss_ctl, &data);
	if (ret) {
		dev_err(fdev->dev, "failed to get calibration_data: %d\n", ret);
		return 0;
	}

	if (fss_control_get_hw_version(fdev->fss_ctl) == 4) {
		ret = __cal_data_valid_v4(info, &data);

		dev_info(fdev->dev, "v4: [%s] volt=%d", ret ? "o" : "x", info->volt);
		dev_info(fdev->dev, "+ fss.cdl0=[b=%#x,s=%#x,t=%#x,v=%#x]\n", fss->bitmap, fss->cdl0_size, fc->target_cdl0, fss->cdl0);
                dev_info(fdev->dev, "+ dsu.cdl0=[b=%#x,s=%#x,t=%#x,v=%#x]\n", dsu->bitmap, dsu->cdl0_size, fc->target_cdl0, dsu->cdl0);
                dev_info(fdev->dev, "+ fss.min_cdl1=[b=%#x,s=%#x,t=%#x,v=%#x]\n", fss->bitmap, fss->cdl1_size, fc->target_min_cdl1, fss->min_cdl1);
                dev_info(fdev->dev, "+ dsu.min_cdl1=[b=%#x,s=%#x,t=%#x,v=%#x]\n",dsu->bitmap, dsu->cdl1_size, fc->target_min_cdl1, dsu->min_cdl1);

		return ret;
	}

	ret = cal_data_valid(fss->bitmap, fss->cdl1_size, fss->min_cdl1, info->fc->target_min_cdl1);
	dev_info(fdev->dev, "v1: [%s] volt=%d", ret ? "o" : "x", info->volt);
        dev_info(fdev->dev, "+ fss.min_cdl1=[b=%#x,s=%#x,t=%#x,v=%#x]\n", fss->bitmap, fss->cdl1_size, fc->target_min_cdl1, fss->min_cdl1);
	return ret;
}

static int fss_scan_iterate_voltage(struct fss_scan_device *fdev, struct fss_scan_freq *fc, int volt_init)
{
	int ret;
	int best = 0;
	int volt_min = fdev->volt_min;
	int target_volt_min;
	int target_volt_max;

	if (!volt_init)
		return 0;

	target_volt_max = target_volt_min = volt_init;

	while (target_volt_min >= volt_min) {
		struct fss_scan_info info = { 0 };

		dev_dbg(fdev->dev, "request volt=(%d-%d)\n", target_volt_min, target_volt_max);

		ret = regulator_set_voltage(fdev->supply, target_volt_min, target_volt_max);
		if (ret) {
			dev_err(fdev->dev, "failed to set volt: %d\n", ret);
			return 0;
		}

		/* must wait more time, when lowering voltage */
		msleep(20);

		info.fc = fc;
		info.volt = regulator_get_voltage(fdev->supply);

		ret = fss_scan_calibrate_and_check(fdev, &info);
		if (!ret)
			return best;

		best = regulator_get_voltage(fdev->supply);
		target_volt_max = best - 1;
		target_volt_min = best - 12500;
	}

	return best;
}

static int fss_scan(struct fss_scan_device *fdev, struct fss_scan_freq *fc, int volt_init)
{
	int ret;

	ret = fss_scan_set_freq_volt(fdev, fc->freq, volt_init);
	if (ret) {
		dev_err(fdev->dev, "failed to set freq=%dMHz, volt=%d\n",
			fc->freq / 1000000, volt_init);
		return 0;
	}

	msleep(200);

	ret = fss_scan_iterate_voltage(fdev, fc, volt_init);

	dev_info(fdev->dev, "get result freq=%d, input=(%d,%d), output=%d\n",
		fc->freq, volt_init, fdev->volt_min, ret);

	return ret;
}

static void fss_scan_work(struct work_struct *work)
{
	struct fss_scan_device *fdev = container_of(work, struct fss_scan_device, work);
	int i, j;
	int volt;
	int error;
	int freeze_task = fdev->freeze_task;

	fdev->supply = regulator_get(fdev->dev, "cpu");
	if (IS_ERR(fdev->supply)) {
		dev_err(fdev->dev, "failed to get regulator: %ld\n",
				PTR_ERR(fdev->supply));
		return;
	}

	fss_scan_save_state(fdev);

	if (freeze_task) {
		error = freeze_processes();
		if (error) {
			dev_err(fdev->dev, "failed to freeze processes: %d\n", error);
			return;
		}
	}

	volt = fdev->volt_max;

	for (i = 0; i < fdev->num_freqs; i++) {
		int best = 0;
		int res;

		for (j = 0; j < fdev->scan_num; j++) {
			res = fss_scan(fdev, &fdev->freqs[i], volt);
			if (res == 0)
				continue;
			if (best == 0 || best > res)
				best = res;
		}

		if (!best) {
			dev_err(fdev->dev, "no voltage found\n");
			break;
		}
		volt = fdev->volts[i] = best;
	}

	if (freeze_task)
		thaw_processes();

	fss_scan_restore_state(fdev);

	regulator_put(fdev->supply);
}

static void fss_scan_wait(struct fss_scan_device *fdev)
{
	flush_work(&fdev->work);
}

static int fss_scan_start(struct fss_scan_device *fdev)
{
	if (!queue_work_on(0, system_highpri_wq, &fdev->work))
		return -EBUSY;
	return 0;
}

static ssize_t freeze_task_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct fss_scan_device *fdev = dev_get_drvdata(dev);
	bool enable;
	int ret;

	ret = strtobool(buf, &enable);
	if (ret)
		return ret;

	fdev->freeze_task = enable;

	return count;
}

static ssize_t freeze_task_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct fss_scan_device *fdev = dev_get_drvdata(dev);

	return  snprintf(buf, PAGE_SIZE, "%d\n", fdev->freeze_task);
}
static DEVICE_ATTR_RW(freeze_task);

static ssize_t control_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct fss_scan_device *fdev = dev_get_drvdata(dev);
	int ret = 0;

	if (!strncmp("start", buf, 5))
		ret = fss_scan_start(fdev);
	else if (!strncmp("wait", buf, 4))
		fss_scan_wait(fdev);
	else
		ret = -EINVAL;

	return ret ?: count;
}
static DEVICE_ATTR_WO(control);

static
ssize_t frequencies_mhz_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	int len = 0;
	int i;
	struct fss_scan_device *fdev = dev_get_drvdata(dev);

	len += snprintf(buf + len, PAGE_SIZE - len, "%d", fdev->freqs[0].freq / 1000000);
	for (i = 1; i < fdev->num_freqs; i++)
		len += snprintf(buf + len, PAGE_SIZE - len, " %d", fdev->freqs[i].freq / 1000000);
	len += snprintf(buf + len, PAGE_SIZE - len, "\n");
	return len;
}
static DEVICE_ATTR_RO(frequencies_mhz);

static ssize_t voltages_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct fss_scan_device *fdev = dev_get_drvdata(dev);
	int len = 0;
	int i;

	len += snprintf(buf + len, PAGE_SIZE - len, "%d", fdev->volts[0]);
	for (i = 1; i < fdev->num_freqs; i++)
		len += snprintf(buf + len, PAGE_SIZE - len, " %d",
				fdev->volts[i]);
	len += snprintf(buf + len, PAGE_SIZE - len, "\n");
	return len;
}
static DEVICE_ATTR_RO(voltages);

static ssize_t target_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct fss_scan_device *fdev = dev_get_drvdata(dev);
	int len = 0;
	int i;

	for (i = 1; i < fdev->num_freqs; i++) {
		struct fss_scan_freq *fc = &fdev->freqs[i];

		len += snprintf(buf + len, PAGE_SIZE - len, "(%d", fc->freq / 1000000);
		if (fdev->freqs[i].target_cdl0)
			len += snprintf(buf + len, PAGE_SIZE - len, ",%#x", fc->target_cdl0);
		len += snprintf(buf + len, PAGE_SIZE - len, ",%#x)\n", fc->target_min_cdl1);
	}
	return len;
}
static DEVICE_ATTR_RO(target);

static struct attribute *fss_scan_attrs[] = {
	&dev_attr_control.attr,
	&dev_attr_voltages.attr,
	&dev_attr_frequencies_mhz.attr,
	&dev_attr_freeze_task.attr,
	&dev_attr_target.attr,
	NULL
};

static struct attribute_group fss_scan_attr_group = {
	.name = "scan",
	.attrs = fss_scan_attrs,
};

static int of_parse_config(struct fss_scan_device *fdev, struct device_node *np, int prop_version)
{
	int ret;
	int i;

	ret = of_property_read_u32(np, "fss-max-voltage", &fdev->volt_max);
	if (ret)
		return ret;

	ret = of_property_read_u32(np, "fss-min-voltage", &fdev->volt_min);
	if (ret)
		return ret;

	if (of_property_read_u32(np, "fss-scan-num", &fdev->scan_num))
		fdev->scan_num = 3;

	if (prop_version == 1) {
		int target;

		ret = of_property_read_u32(np, "fss-target", &target);
		if (ret)
			return ret;

		ret = of_property_count_u32_elems(np, "fss-frequencies");
		if (ret < 0)
			return ret;
		fdev->num_freqs = ret;

		fdev->freqs = devm_kcalloc(fdev->dev, fdev->num_freqs, sizeof(*fdev->freqs), GFP_KERNEL);
		if (!fdev->freqs)
			return -ENOMEM;

		for (i = 0; i < fdev->num_freqs; i++) {
			struct fss_scan_freq *fc = &fdev->freqs[i];

			of_property_read_u32_index(np, "fss-frequencies", i, &fc->freq);
			fc->target_min_cdl1 = target;
			dev_info(fdev->dev, "freq=%d,target=%#x\n", fc->freq, fc->target_min_cdl1);
		}
	} else {
		const unsigned int *p;

		p = (const unsigned int *)of_get_property(np, "fss-frequency-config", &ret);
		if (!p || (ret % 12) != 0)
			return -EINVAL;
		fdev->num_freqs = ret / 12;

		fdev->freqs = devm_kcalloc(fdev->dev, fdev->num_freqs, sizeof(*fdev->freqs), GFP_KERNEL);
		if (!fdev->freqs)
			return -ENOMEM;

		for (i = 0; i < fdev->num_freqs; i++) {
			struct fss_scan_freq *fc = &fdev->freqs[i];

			fc->freq = be32_to_cpu(*p++);
			fc->target_cdl0 = be32_to_cpu(*p++);
			fc->target_min_cdl1 = be32_to_cpu(*p++);
			dev_info(fdev->dev, "freq=%d,target=%#x,%#x\n", fc->freq, fc->target_cdl0, fc->target_min_cdl1);
		}

	}
	return 0;
}

static int fss_scan_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	struct fss_scan_device *fdev;
	int ret;

	fdev = devm_kzalloc(dev, sizeof(*fdev), GFP_KERNEL);
	if (!fdev)
		return -ENOMEM;
	fdev->dev = dev;

	fdev->fss_ctl = of_fss_control_get(np);
	if (IS_ERR(fdev->fss_ctl)) {
		ret = PTR_ERR(fdev->fss_ctl);
		dev_err(dev, "failed to get fss control: %d\n", ret);
		return ret;
	};

	ret = of_parse_config(fdev, np, fss_control_get_hw_version(fdev->fss_ctl) == 4 ? 2 : 1);
	if (ret) {
		dev_info(dev, "failed to get config from dt: %d\n", ret);
		return ret;
	}

	fdev->volts = devm_kcalloc(dev, fdev->num_freqs, sizeof(*fdev->volts), GFP_KERNEL);
	if (!fdev->volts)
		return -ENOMEM;

	fdev->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(fdev->clk)) {
		ret = PTR_ERR(fdev->clk);
		if (ret == -EPROBE_DEFER)
			dev_dbg(dev, "clk is not ready, retry\n");
		else
			dev_err(dev, "failed to get clk: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, fdev);
	INIT_WORK(&fdev->work, fss_scan_work);

	ret = sysfs_create_group(&dev->kobj, &fss_scan_attr_group);
	if (ret) {
		dev_err(dev, "failed to create sysfs group: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct of_device_id fss_scan_ids[] = {
	{ .compatible = "realtek,fss-scan" },
	{}
};

static struct platform_driver fss_scan_drv = {
	.driver = {
		.name           = "rtk-fss-scan",
		.owner          = THIS_MODULE,
		.of_match_table = of_match_ptr(fss_scan_ids),
	},
	.probe    = fss_scan_probe,
};
module_platform_driver(fss_scan_drv);

MODULE_DESCRIPTION("Realtek FSS Scan driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:rtk-fss-scan");

