// SPDX-License-Identifier: GPL-2.0-only
/*
 * cc-tee.c - TEE clock controller
 *
 * Copyright (C) 2019,2023 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */

#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/tee_drv.h>
#include <linux/uuid.h>

#define HZ_PER_MHZ            1000000
#define TA_CMD_SCPU_PLL       5

struct rtk_optee_clk_data;

struct rtk_optee_clk_ops {
	int (*set_cpu_clk_freq)(struct rtk_optee_clk_data *data, unsigned long freq_mhz);
};

struct rtk_optee_clk_data {
	struct device *dev;
	struct tee_context *ctx;
	u32 session_id;
	const uuid_t *uuid;
	const struct rtk_optee_clk_ops *ops;
};

static int rtk_optee_clk_setup(struct rtk_optee_clk_data *data);

static int rtk_optee_clk_set_cpu_clk_freq(struct rtk_optee_clk_data *data, unsigned long freq)
{
	struct tee_ioctl_invoke_arg inv_arg;
	struct tee_param param[4];
	int ret;
	int retry = 0;

again:
	memset(&inv_arg, 0, sizeof(inv_arg));
	memset(&param, 0, sizeof(param));

	inv_arg.func = TA_CMD_SCPU_PLL;
	inv_arg.session = data->session_id;
	inv_arg.num_params = 4;

	param[0].u.value.a = freq;
	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INOUT;
	param[1].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	param[2].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
	param[3].attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;

	ret = tee_client_invoke_func(data->ctx, &inv_arg, param);
	if (inv_arg.ret != 0 && retry == 0) {
		dev_warn(data->dev, "failed to invoke, retry\n");
		rtk_optee_clk_setup(data);
		retry++;
		goto again;
	}

	if ((ret < 0) || (inv_arg.ret != 0)) {
		dev_err(data->dev, "failed to invoke func: %d, %x\n", ret,
			inv_arg.ret);
		return -EINVAL;
	}

	if (param[0].u.value.b) {
		dev_err(data->dev, "failed to set freq: %lld\n",
			param[0].u.value.b);
		return -EINVAL;
	}

	return 0;
}

static const struct rtk_optee_clk_ops rtk_optee_clk_ops = {
	.set_cpu_clk_freq = rtk_optee_clk_set_cpu_clk_freq,
};

static int rtk_optee_clk_optee_match(struct tee_ioctl_version_data *vers,
				     const void *data)
{
	if (vers->impl_id == TEE_IMPL_ID_OPTEE)
		return 1;
	return 0;
}

static int rtk_optee_clk_setup(struct rtk_optee_clk_data *data)
{
	struct tee_ioctl_open_session_arg arg = { 0 };
	int ret;

	data->ctx = tee_client_open_context(NULL, rtk_optee_clk_optee_match, NULL, NULL);
	if (IS_ERR(data->ctx)) {
		ret = PTR_ERR(data->ctx);
		if (ret == -ENOENT)
			ret = -EPROBE_DEFER;
		return dev_err_probe(data->dev, ret, "failed to open context\n");
	}

	export_uuid(arg.uuid, data->uuid);
	arg.clnt_login = TEE_IOCTL_LOGIN_PUBLIC;
	arg.num_params = 0;

	ret = tee_client_open_session(data->ctx, &arg, NULL);
	if (ret) {
		dev_err(data->dev, "failed to open session: %d\n", ret);
		return ret;
	}

	data->session_id = arg.session;
	data->ops = &rtk_optee_clk_ops;
	return 0;
}

static void rtk_optee_clk_teardown(struct rtk_optee_clk_data *data)
{
	tee_client_close_session(data->ctx, data->session_id);
	tee_client_close_context(data->ctx);
}

static int rtk_optee_clk_probe(struct device *dev)
{
	struct tee_client_device *client_dev = to_tee_client_device(dev);
	struct rtk_optee_clk_data *data;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dev = dev;
	data->uuid = &client_dev->id.uuid;

	ret = rtk_optee_clk_setup(data);
	if (ret)
		return ret;

	dev_set_drvdata(dev, data);
	return 0;
}

static int rtk_optee_clk_remove(struct device *dev)
{
	struct rtk_optee_clk_data *data = dev_get_drvdata(dev);

	dev_set_drvdata(dev, NULL);
	rtk_optee_clk_teardown(data);
	return 0;
}

static const struct tee_client_device_id rtk_optee_clk_ids[] = {
	{ UUID_INIT(0x650b79a1,
		    0xa79a, 0x43ea, 0x91, 0x85, 0xf6, 0x67, 0x55, 0x65, 0x64, 0xa7) },
	{}
};
MODULE_DEVICE_TABLE(tee, rtk_optee_clk_ids);

static struct tee_client_driver rtk_optee_clk_driver = {
	.id_table	= rtk_optee_clk_ids,
	.driver		= {
		.name  = "rtk-optee-clk",
		.bus   = &tee_bus_type,
		.probe = rtk_optee_clk_probe,
		.remove = rtk_optee_clk_remove,
	},
};

struct rtk_tee_cc_data {
	struct device *dev;
	struct clk_hw hw;
	unsigned long freq_mhz;
	struct rtk_optee_clk_data *optee_data;
};

static long rtk_tee_cc_clk_round_rate(struct clk_hw *hw, unsigned long rate,
				      unsigned long *parent_rate)
{
	return DIV_ROUND_CLOSEST(rate, 100000000) * 100000000;
}

static unsigned long rtk_tee_cc_clk_recalc_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	struct rtk_tee_cc_data *cc_data = container_of(hw, struct rtk_tee_cc_data, hw);

	dev_dbg(cc_data->dev, "freq = %ld\n", cc_data->freq_mhz * HZ_PER_MHZ);
	return cc_data->freq_mhz * HZ_PER_MHZ;
}

static int rtk_tee_cc_clk_set_rate(struct clk_hw *hw, unsigned long rate,
				   unsigned long parent_rate)
{
	struct rtk_tee_cc_data *cc_data = container_of(hw, struct rtk_tee_cc_data, hw);
	unsigned long freq_mhz = rate / HZ_PER_MHZ;
	struct device *dev = cc_data->dev;
	int ret = 0;
	ktime_t start;
	s64 delta_us;

	dev_dbg(dev, "enter %s (freq=%lu)\n", __func__, freq_mhz);

	start = ktime_get();
	ret = cc_data->optee_data->ops->set_cpu_clk_freq(cc_data->optee_data, freq_mhz);
	if (ret)
		goto done;
	cc_data->freq_mhz = freq_mhz;
done:
	delta_us = ktime_to_us(ktime_sub(ktime_get(), start));
	dev_dbg(dev, "exit %s (freq=%lu, time=%lld, ret=%d)\n", __func__,
		freq_mhz, delta_us,  ret);
	return ret;
}

static const struct clk_ops rtk_tee_cc_clk_ops = {
	.round_rate       = rtk_tee_cc_clk_round_rate,
	.recalc_rate      = rtk_tee_cc_clk_recalc_rate,
	.set_rate         = rtk_tee_cc_clk_set_rate,
};

static int optee_clk_driver_match(struct device *dev, const void *data)
{
	return 1;
}

static void teardown_tee(void *d)
{
	struct rtk_optee_clk_data *data = d;
	struct device *dev = data->dev;

	dev_set_drvdata(dev, NULL);
	rtk_optee_clk_teardown(data);
}

static int rtk_tee_cc_legacy_setup(struct device *dev)
{
	struct rtk_optee_clk_data *data;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dev = dev;
	data->uuid = &rtk_optee_clk_ids->uuid;
	ret = rtk_optee_clk_setup(data);
	if (ret)
		return ret;

	dev_set_drvdata(dev, data);

	return devm_add_action_or_reset(dev, teardown_tee, data);
}

static int rtk_tee_cc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rtk_tee_cc_data *cc_data;
	int ret;
	struct clk *clk;
	struct device *optee_dev;

	cc_data = devm_kzalloc(dev, sizeof(*cc_data), GFP_KERNEL);
	if (!cc_data)
		return -ENOMEM;

	ret = driver_register(&rtk_optee_clk_driver.driver);
	if (ret) {
		dev_err(dev, "failed to register optee_clk driver: %d\n", ret);
		return ret;
	}

	optee_dev = driver_find_device(&rtk_optee_clk_driver.driver, NULL, NULL,
				       optee_clk_driver_match);
	if (!optee_dev) {
		/* remove tee client driver in legacy flow */
		driver_unregister(&rtk_optee_clk_driver.driver);

		dev_info(dev, "use legacy flow\n");
		ret = rtk_tee_cc_legacy_setup(dev);
		if (ret) {
			if (ret == -ENOENT)
				ret = -EPROBE_DEFER;
			return dev_err_probe(dev, ret, "failed in rtk_tee_cc_legacy_setup()\n");
		}
		optee_dev = dev;
	}
	if (!optee_dev) {
		dev_err(dev, "no optee_clk device\n");
		driver_unregister(&rtk_optee_clk_driver.driver);
		return -ENODEV;
	}

	cc_data->dev = dev;
	cc_data->hw.init = CLK_HW_INIT("pll_scpu", "osc27m", &rtk_tee_cc_clk_ops, 0);
	cc_data->optee_data = dev_get_drvdata(optee_dev);

	clk = devm_clk_register(dev, &cc_data->hw);
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		dev_err(dev, "failed to register clk: %d\n", ret);
		goto error;
	}

	of_clk_add_provider(dev->of_node, of_clk_src_simple_get, clk);
	clk_register_clkdev(clk, __clk_get_name(clk), NULL);

	return 0;

error:
	if (optee_dev != dev)
		driver_unregister(&rtk_optee_clk_driver.driver);

	return ret;
}

static const struct of_device_id rtk_tee_cc_of_ids[] = {
	{ .compatible = "realtek,tee-clock-controller", },
	{}
};
MODULE_DEVICE_TABLE(of, rtk_tee_cc_of_ids);

static struct platform_driver rtk_tee_cc_driver = {
	.probe = rtk_tee_cc_probe,
	.driver = {
		.name = "rtk-tee-cc",
		.of_match_table = of_match_ptr(rtk_tee_cc_of_ids),
	},
};

static int __init rtk_tee_cc_init(void)
{
	return platform_driver_register(&rtk_tee_cc_driver);
}
module_init(rtk_tee_cc_init);

MODULE_DESCRIPTION("Reatek Tee Clock Controller");
MODULE_AUTHOR("Cheng-Yu Lee <cylee12@realtek.com>");
MODULE_LICENSE("GPL v2");
