// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2018,2020 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */

#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/pm_clock.h>
#include <linux/pm_runtime.h>
#include <linux/module.h>
#include "common.h"
#include "audio_aio_rpc.h"

struct rtk_aio_ctrl_desc {
	unsigned int ai_ctrl_reg;
	int (*init_func)(struct device *dev);
};

struct rtk_aio_ctrl_data {
	const struct rtk_aio_ctrl_desc *desc;
	struct rtk_aio_ctrl_rpc rpc;
};

static
int rtk_aio_ctrl_send_msg(struct device *dev, unsigned int command, void *data, int size)
{
	struct rtk_aio_ctrl_data *aio = dev_get_drvdata(dev);
	struct rtk_aio_ctrl_buf buf = {};
	int ret;

	ret = rtk_aio_ctrl_alloc_buf(dev, &buf);
	if (ret)
		return ret;

	ret = rtk_aio_ctrl_copy_to_buf(&buf, data, size);
	if (ret)
		goto exit;

	ret = rtk_aio_ctrl_rpc_send_msg(&aio->rpc, command, buf.dma, size);
exit:
	rtk_aio_ctrl_free_buf(&buf);
	return ret;
}

struct rtk_aio_ctrl_ai_params {
	u32 i2s_conf;
};

static int rtk_aio_ctrl_parse_ai_params(struct device_node *np,
					struct rtk_aio_ctrl_ai_params *ai_params)
{
	if (of_property_match_string(np, "realtek,ai-type", "i2s") < 0)
		return 0;

	ai_params->i2s_conf = RTK_AUDIO_IN_I2S_STATUS;
	if (of_property_read_bool(np, "realtek,ai-i2s-pin-shared"))
		ai_params->i2s_conf |= RTK_AUDIO_IN_I2S_PIN_SHARED;
	if (of_property_read_bool(np, "realtek,ai-i2s-master"))
		ai_params->i2s_conf |= RTK_AUDIO_IN_I2S_MASTER;

	return 0;
}

static int rtk_aio_ctrl_init_remote_ai(struct device *dev)
{
	struct rtk_aio_ctrl_data *aio = dev_get_drvdata(dev);
	struct AUDIO_RPC_AIO_PRIVATEINFO_PARAMETERS params = { 0 };
	struct rtk_aio_ctrl_ai_params ai_params = { 0 };
	int ret;

	ret = rtk_aio_ctrl_parse_ai_params(dev->of_node, &ai_params);
	if (ret)
		return ret;

	params.type = rtk_aio_ctrl_rpc_to_remote32(&aio->rpc, ENUM_PRIVATEINFO_AIO_AI_INTERFACE_SWITCH_CONTROL);
	params.argateInfo[0] = rtk_aio_ctrl_rpc_to_remote32(&aio->rpc, BIT(ENUM_DT_AI_AIN) | BIT(aio->desc->ai_ctrl_reg));
	params.argateInfo[1] = rtk_aio_ctrl_rpc_to_remote32(&aio->rpc, ai_params.i2s_conf);
	return rtk_aio_ctrl_send_msg(dev, ENUM_KERNEL_RPC_AIO_PRIVATEINFO, &params, sizeof(params));
}

static const struct of_device_id rtk_aio_ctrl_ao_dev_match[] = {
	{ .compatible = "realtek,audio-out-dac",    .data = (void *)ENUM_DT_AO_DAC },
	{ .compatible = "realtek,audio-out-i2s",    .data = (void *)ENUM_DT_AO_I2S },
	{ .compatible = "realtek,audio-out-spdif",  .data = (void *)ENUM_DT_AO_SPDIF },
	{ .compatible = "realtek,audio-out-hdmi",   .data = (void *)ENUM_DT_AO_HDMI },
	{ .compatible = "realtek,audio-out-global", .data = (void *)ENUM_DT_AO_GLOBAL },
};

struct rtk_aio_ctrl_ao_params {
	u32 bitmap;
	u32 i2s_ch;
	u32 i2s_mode;
};

static int rtk_aio_ctrl_parse_ao_params(struct device_node *np,
                                        struct rtk_aio_ctrl_ao_params *ao_params)
{
	struct device_node *child;

	ao_params->bitmap = 0;

	for_each_child_of_node(np, child) {
		const struct of_device_id *match;
		u32 id;

		match = of_match_node(rtk_aio_ctrl_ao_dev_match, child);
		if (!match)
			continue;

		id = (unsigned long)(match->data);
		ao_params->bitmap |= BIT(id);


		if (!of_device_is_available(child))
			continue;

		if (id == ENUM_DT_AO_I2S) {
			u32 val = 0;

			ao_params->i2s_ch = 2;
			ao_params->i2s_mode = RTK_AUDIO_OUT_I2S_MODE_MASTER;

			if (of_property_read_u32(child, "realtek,ao-i2s-channel", &val))
				val = 0;
			if (val == 8)
				ao_params->i2s_ch = 8;

			if (of_property_read_bool(child, "realtek,ao-i2s-slave"))
				ao_params->i2s_mode = RTK_AUDIO_OUT_I2S_MODE_SLAVE;
		}

		ao_params->bitmap &= ~BIT(id);
	}
	return 0;
}


static int rtk_aio_ctrl_init_remote_ao(struct device *dev)
{
	struct rtk_aio_ctrl_data *aio = dev_get_drvdata(dev);
	struct AUDIO_RPC_AIO_PRIVATEINFO_PARAMETERS params = { 0 };
	struct rtk_aio_ctrl_ao_params ao_params = { 0 };
	int ret;

	ret = rtk_aio_ctrl_parse_ao_params(dev->of_node, &ao_params);
	if (ret)
		return ret;

	params.type = rtk_aio_ctrl_rpc_to_remote32(&aio->rpc, ENUM_PRIVATEINFO_AIO_AO_INTERFACE_SWITCH_CONTROL);
	params.argateInfo[0] = rtk_aio_ctrl_rpc_to_remote32(&aio->rpc, ao_params.bitmap);
	params.argateInfo[1] = rtk_aio_ctrl_rpc_to_remote32(&aio->rpc, ao_params.i2s_ch);
	params.argateInfo[2] = rtk_aio_ctrl_rpc_to_remote32(&aio->rpc, ao_params.i2s_mode);
	return rtk_aio_ctrl_send_msg(dev, ENUM_KERNEL_RPC_AIO_PRIVATEINFO, &params, sizeof(params));
}

static int rtk_aio_ctrl_init_default(struct device *dev)
{
	return 0;
}

static const struct rtk_aio_ctrl_desc ai_adc = {
	.ai_ctrl_reg = ENUM_DT_AI_ADC,
	.init_func   = rtk_aio_ctrl_init_remote_ai,
};

static const struct rtk_aio_ctrl_desc ai_analog_in = {
	.ai_ctrl_reg = ENUM_DT_AI_ANALOG_IN,
	.init_func   = rtk_aio_ctrl_init_remote_ai,
};

static const struct rtk_aio_ctrl_desc ai_adc_amic = {
	.ai_ctrl_reg = ENUM_DT_AI_ADC_AMIC,
	.init_func   = rtk_aio_ctrl_init_remote_ai,
};

static const struct rtk_aio_ctrl_desc ai_adc_earc_combo = {
	.ai_ctrl_reg = ENUM_DT_AI_EARC_COMBO,
	.init_func   = rtk_aio_ctrl_init_remote_ai,
};

static const struct rtk_aio_ctrl_desc ai_adc_spdif_in = {
	.init_func   = rtk_aio_ctrl_init_default,
};

static const struct rtk_aio_ctrl_desc ao = {
	.init_func   = rtk_aio_ctrl_init_remote_ao,
};

static const struct of_device_id rtk_aio_ctrl_of_match[] = {
	{ .compatible = "realtek,audio-in-adc",        .data = &ai_adc,  },
	{ .compatible = "realtek,audio-in-analog-in",  .data = &ai_analog_in, },
	{ .compatible = "realtek,audio-in-adc-amic",   .data = &ai_adc_amic, },
	{ .compatible = "realtek,audio-in-earc-combo", .data = &ai_adc_earc_combo, },
	{ .compatible = "realtek,audio-spdif-in",      .data = &ai_adc_spdif_in, },
	{ .compatible = "realtek,audio-out",           .data = &ao, },
	{}
};

static void rtk_aio_ctrl_setup_gpios(struct device *dev)
{
	struct gpio_desc *gpio_desc;
	int num;
	int i;

	num = gpiod_count(dev, "audio");
	for (i = 0; i < num; i++) {
		gpio_desc = gpiod_get_index(dev, "audio", i, GPIOD_OUT_HIGH);

		if (IS_ERR(gpio_desc)) {
			dev_err(dev, "failed to get gpio audio%d\n", i);
			continue;
		}

		dev_info(dev, "request gpio%d output high\n", desc_to_gpio(gpio_desc));
		gpiod_put(gpio_desc);
	}
}

static int rtk_aio_ctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret;
	struct rtk_aio_ctrl_data *aio;

	aio = devm_kzalloc(dev, sizeof(*aio), GFP_KERNEL);
	if (!aio)
		return -ENOMEM;

	aio->desc = of_device_get_match_data(dev);
	if (!aio->desc || !aio->desc->init_func)
		return -EINVAL;

	platform_set_drvdata(pdev, aio);

	/* only setup rpc for remote ai/ao */
	if (aio->desc->init_func != rtk_aio_ctrl_init_default) {
		ret = rtk_aio_ctrl_rpc_setup(dev, &aio->rpc);
		if (ret)
			return ret;
	}

	rtk_aio_ctrl_setup_gpios(dev);

	ret = pm_clk_create(dev);
	if (ret) {
		dev_err(dev, "failed in pm_clk_create(): %d\n", ret);
		return ret;
	}
	of_pm_clk_add_clks(dev);

	ret = aio->desc->init_func(dev);
	if (ret) {
		pm_clk_destroy(dev);
		return ret;
	}

	pm_runtime_set_suspended(dev);
	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);

	return 0;
}

static int rtk_aio_ctrl_remove(struct platform_device *pdev)
{
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static struct platform_driver rtk_aio_ctrl_driver = {
	.driver = {
		.name   = "rtk-aio-ctrl",
		.of_match_table = rtk_aio_ctrl_of_match,
	},
	.probe = rtk_aio_ctrl_probe,
	.remove = rtk_aio_ctrl_remove,
};

static int __init rtk_aio_ctrl_init(void)
{
	return platform_driver_register(&rtk_aio_ctrl_driver);
}
late_initcall(rtk_aio_ctrl_init);

static void __exit rtk_aio_ctrl_exit(void)
{
	platform_driver_unregister(&rtk_aio_ctrl_driver);
}
module_exit(rtk_aio_ctrl_exit);

MODULE_LICENSE("GPL v2");
