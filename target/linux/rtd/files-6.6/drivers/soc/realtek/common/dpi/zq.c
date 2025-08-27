// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */

#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/of.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/thermal.h>

#include "dpi.h"
#include "dpi_reg.h"

struct dpi_zq_cal_priv {
	struct device *dev;
	struct dpi_device *dpi;
	struct notifier_block nb;

	uint32_t zq_vref[3];
	uint32_t zprog[5];
	uint32_t zq_nocd2_en[5];
	uint32_t zprog_nocd2[5];
	uint32_t zq_vref_num;
	uint32_t zprog_num;
	uint32_t zq_nocd2_en_num;
	uint32_t zprog_nocd2_num;
};

static void dpi_zq_pd_enable(struct dpi_zq_cal_priv *priv)
{
	struct dpi_device *dpi = priv->dpi;
	pr_debug("%s\n", __func__);
	dpi_reg_write(dpi, 0x60, dpi->crt_ctl_2);
	dpi_reg_update_bits(dpi, DPI_DLL_CRT_ZQ_PAD_CTRL, 0x2001, 0x2001);
	dpi_reg_update_bits(dpi, DPI_DLL_CRT_DPI_CTRL_1, 0x3, 0x3);             // fw_set_wr_dly
	ndelay(100);
}

static void dpi_zq_pd_disable(struct dpi_zq_cal_priv *priv)
{
	struct dpi_device *dpi = priv->dpi;
	pr_debug("%s\n", __func__);
	dpi_reg_read(dpi, 0x60, &dpi->crt_ctl_2);
	dpi_reg_update_bits(dpi, 0x60, BIT(19), 0x0);
	dpi_reg_update_bits(dpi, DPI_DLL_CRT_ZQ_PAD_CTRL, 0x2001, 0x0);
	dpi_reg_update_bits(dpi, DPI_DLL_CRT_DPI_CTRL_1, 0x3, 0x3);             // fw_set_wr_dly
}


static void dpi_zq_cal_print_result(struct dpi_zq_cal_priv *priv)
{
	struct dpi_device *dpi = priv->dpi;
	uint32_t zq_cal_r480_res, zq_cal_odtp_res, zq_cal_odtn_res, zq_cal_ocdp_res, zq_cal_ocdn_res;

	dpi_reg_read(dpi, DPI_DLL_PAD_RZCTRL_STATUS, &zq_cal_r480_res);
	dpi_reg_read(dpi, DPI_DLL_ODT_TTCP0_SET0, &zq_cal_odtp_res);
	dpi_reg_read(dpi, DPI_DLL_ODT_TTCN0_SET0, &zq_cal_odtn_res);
	dpi_reg_read(dpi, DPI_DLL_OCDP0_SET0, &zq_cal_ocdp_res);
	dpi_reg_read(dpi, DPI_DLL_OCDN0_SET0, &zq_cal_ocdn_res);
	pr_info("%s: rzq_480code = %08x\n", __func__, (zq_cal_r480_res >> 1));
	pr_info("%s: ODTP set 3/2/1/0 = %08x\n", __func__, zq_cal_odtp_res);
	pr_info("%s: ODTN set 3/2/1/0 = %08x\n", __func__, zq_cal_odtn_res);
	pr_info("%s: OCDP set 3/2/1/0 = %08x\n", __func__, zq_cal_ocdp_res);
	pr_info("%s: OCDN set 3/2/1/0 = %08x\n", __func__, zq_cal_ocdn_res);
}

static int dpi_zq_cal_run_r480(struct dpi_zq_cal_priv *priv, uint32_t vref_val)
{
	struct dpi_device *dpi = priv->dpi;
	int ret;

	pr_debug("%s\n", __func__);

	dpi_reg_write(dpi, DPI_DLL_CRT_PAD_CTRL_PROG, 0x04008c19);
	dpi_reg_write(dpi, DPI_DLL_CRT_ZQ_PAD_CTRL, vref_val);
	if(dpi->type == DPI_DRAM_TYPE_DDR4) dpi_reg_write(dpi, DPI_DLL_CRT_ZQ_NOCD2, 0x00000001);
	else dpi_reg_write(dpi, DPI_DLL_CRT_ZQ_NOCD2, 0x00000000);
	//dpi_reg_update_bits(dpi, DPI_DLL_CRT_DPI_CTRL_0, 1, 1);
	dpi_reg_update_bits(dpi, DPI_DLL_CRT_DPI_CTRL_1, 0x3, 0x3);             // fw_set_wr_dly
	//dpi_reg_update_bits(dpi, DPI_DLL_CRT_DPI_CTRL_0, 1, 0);

	dpi_reg_update_bits(dpi, DPI_DLL_CRT_PAD_CTRL_PROG, BIT(16), BIT(16));
	ret = dpi_reg_poll_bit(dpi, DPI_DLL_CRT_INT_STATUS_2, 10);
	dpi_reg_write(dpi, DPI_DLL_CRT_INT_STATUS_2, 0x00000000);
	dpi_reg_update_bits(dpi, DPI_DLL_CRT_PAD_CTRL_PROG, BIT(16), 0);
	return ret;
}

static int dpi_zq_cal_run_set(struct dpi_zq_cal_priv *priv)
{
	struct dpi_device *dpi = priv->dpi;
	int ret;
	int i;

	pr_debug("%s\n", __func__);

	dpi_reg_update_bits(dpi, DPI_DLL_CRT_PAD_CTRL_PROG, BIT(27) | BIT(17), BIT(27) | BIT(17));	// dzq_auto_up = 1, zctrl_filter_en = 1

	for (i = 0; i < priv->zprog_num; i++) {
		// pr_info("%-20s: set%d\n", __func__, i);

		dpi_reg_update_bits(dpi, DPI_DLL_CRT_PAD_CTRL_PROG, 0x70000000, (i << 28));

		if(priv->zq_nocd2_en[i]) dpi_reg_write(dpi, DPI_DLL_CRT_ZQ_PAD_CTRL, priv->zq_vref[2]);
		else dpi_reg_write(dpi, DPI_DLL_CRT_ZQ_PAD_CTRL, priv->zq_vref[1]);

		dpi_reg_write(dpi, DPI_DLL_CRT_ZQ_NOCD2, priv->zq_nocd2_en[i]);
		dpi_reg_write(dpi, DPI_DLL_CRT_PAD_CTRL_ZPROG, priv->zprog[i]);
		dpi_reg_write(dpi, DPI_DLL_CRT_PAD_NOCD2_ZPROG, priv->zprog_nocd2[i]);

		//dpi_reg_update_bits(dpi, DPI_DLL_CRT_DPI_CTRL_0, 1, 1);
		dpi_reg_update_bits(dpi, DPI_DLL_CRT_DPI_CTRL_1, 0x3, 0x3);             // fw_set_wr_dly
		//dpi_reg_update_bits(dpi, DPI_DLL_CRT_DPI_CTRL_0, 1, 0);

		dpi_reg_update_bits(dpi, DPI_DLL_CRT_PAD_CTRL_PROG, BIT(24), BIT(24));
		ret = dpi_reg_poll_bit(dpi, DPI_DLL_CRT_PAD_ZCTRL_STATUS, 31);
		dpi_reg_update_bits(dpi, DPI_DLL_CRT_PAD_CTRL_PROG, BIT(24), 0x0);

		if (ret) {
			dev_err(priv->dev, "failed at calibration set%d\n", i);
			return ret;
		}
	}

	return ret;
}

static int dpi_zq_cal_start_calibration(struct dpi_zq_cal_priv *priv)
{
	int ret;

	ret = dpi_zq_cal_run_r480(priv, priv->zq_vref[0]);
	if (ret) {
		dev_err(priv->dev, "failed at r480 calibration\n");
		return ret;
	}

	ret = dpi_zq_cal_run_set(priv);
	if (ret) {
		dev_err(priv->dev, "failed at zq calibration\n");
		return ret;
	}

	dpi_zq_cal_print_result(priv);

	return 0;
}

static int dpi_zq_cal_cb(struct notifier_block *nb, unsigned long event,
		      void *data)
{
	struct dpi_zq_cal_priv *priv = container_of(nb, struct dpi_zq_cal_priv, nb);
	struct dpi_device *dpi = priv->dpi;
	// struct dpi_event_base_temp_data *d = (struct dpi_event_base_temp_data *)data;
	// int dt = d->new_temp - d->old_temp;

	if (event != DPI_EVENT_BASE_TEMP_DIFF_OVER_THERSHOLD)
		return NOTIFY_DONE;

	dpi_clock_gating_disable(dpi);

	dpi_zq_pd_disable(priv);

	dpi_zq_cal_start_calibration(priv);

	dpi_zq_pd_enable(priv);

	dpi_clock_gating_enable(dpi);

	return NOTIFY_OK;
}

static int of_dpi_zq_cal_parse_data(struct device_node *np, struct dpi_zq_cal_priv *priv)
{
	struct dpi_device *dpi = priv->dpi;
	const char *name[4] = {"zq-vref-ddr4", "zprog-values-ddr4", "zq-ena-nocd2-ddr4", "zprog-nocd2-ddr4"};
	int len, ret;

	if (dpi->type == DPI_DRAM_TYPE_DDR3){
		name[0] = "zq-vref-ddr3";
		name[1] = "zprog-values-ddr3";
		name[2] = "zq-ena-nocd2-ddr3";
		name[3] = "zprog-nocd2-ddr3";
	}
	else if (dpi->type == DPI_DRAM_TYPE_LPDDR4) {
		name[0] = "zq-vref-lpddr4";
		name[1] = "zprog-values-lpddr4";
		name[2] = "zq-ena-nocd2-lpddr4";
		name[3] = "zprog-nocd2-lpddr4";
	}

	if (!of_find_property(np, name[0], &len))
		return -EINVAL;

	if (len > sizeof(priv->zq_vref))
		return -EINVAL;
	len /= 4;
	priv->zq_vref_num = len;

	if((ret = of_property_read_u32_array(np, name[0], priv->zq_vref, len)))
		return ret;

	if (!of_find_property(np, name[1], &len))
		return -EINVAL;

	if (len > sizeof(priv->zprog))
		return -EINVAL;
	len /= 4;
	priv->zprog_num = len;

	if((ret = of_property_read_u32_array(np, name[1], priv->zprog, len)))
		return ret;

	if (!of_find_property(np, name[2], &len))
		return -EINVAL;

	if (len > sizeof(priv->zq_nocd2_en))
		return -EINVAL;
	len /= 4;
	priv->zq_nocd2_en_num = len;

	if((ret = of_property_read_u32_array(np, name[2], priv->zq_nocd2_en, len)))
		return ret;

	if (!of_find_property(np, name[3], &len))
		return -EINVAL;

	if (len > sizeof(priv->zprog_nocd2))
		return -EINVAL;
	len /= 4;
	priv->zprog_nocd2_num = len;

	if((ret = of_property_read_u32_array(np, name[3], priv->zprog_nocd2, len)))
		return ret;

	return ret;
}

static int dpi_zq_cal_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dpi_device *dpi = dev_get_drvdata(dev->parent);
	struct dpi_zq_cal_priv *priv;
	int ret;

	if (!dpi)
		return -EPROBE_DEFER;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dpi = dpi;
	priv->dev = dev;
	priv->nb.notifier_call = dpi_zq_cal_cb;

	ret = of_dpi_zq_cal_parse_data(dev->of_node, priv);
	if (ret) {
		dev_err(dev, "failed to parse data from dt: %d\n", ret);
		return ret;
	}

	return dpi_register_notifier(dpi, &priv->nb);
}

static const struct of_device_id dpi_zq_cal_of_match[] = {
	{ .compatible = "realtek,dpi-zq-cal" },
	{}
};

static struct platform_driver dpi_zq_cal_driver = {
	.driver = {
		.name           = "rtk-dpi-zq-cal",
		.owner          = THIS_MODULE,
		.of_match_table = of_match_ptr(dpi_zq_cal_of_match),
	},
	.probe    = dpi_zq_cal_probe,
};
module_platform_driver(dpi_zq_cal_driver);

MODULE_LICENSE("GPL v2");
