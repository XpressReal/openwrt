// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */

#include <linux/io.h>
#include <linux/of.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/notifier.h>
#include "dpi.h"
#include "dpi_reg.h"

#define DPI_TPT_STATE_0  -1
#define DPI_TPT_STATE_1  1

struct dpi_tx_phase_priv {
	struct device *dev;
	struct dpi_device *dpi;
	struct notifier_block nb;
	int state;
	int ck_phase_default_ofs;
	int ck_phase_ofs;
	uint32_t *ddr_type_q;
	uint32_t *pi_idx_q;
	int32_t *pi_val_q_0;
	int32_t *pi_val_q_1;
	uint32_t num_q;
};

struct dpi_tx_phase_data {
	int ofs;
	int sft;
};

#define DPI_PHASE(_ofs, _sft) { .ofs = _ofs, .sft = _sft, }

static struct dpi_tx_phase_data dpi_tx_phase[]= {
	DPI_PHASE(DPI_DLL_CRT_PLL_PI0,  0),		// CK0
	DPI_PHASE(DPI_DLL_CRT_PLL_PI3, 24),		// CK1
	DPI_PHASE(DPI_DLL_CRT_PLL_PI0,  8),		// CMD
	DPI_PHASE(DPI_DLL_CRT_PLL_PI2, 24),		// CS0
	DPI_PHASE(DPI_DLL_CRT_PLL_PI3,  0),		// CS1
	DPI_PHASE(DPI_DLL_CRT_PLL_PI3,  8),		// CS2
	DPI_PHASE(DPI_DLL_CRT_PLL_PI3, 16),		// CS3
	DPI_PHASE(DPI_DLL_CRT_PLL_PI0, 16),		// DQS0
	DPI_PHASE(DPI_DLL_CRT_PLL_PI0, 24),		// DQS1
	DPI_PHASE(DPI_DLL_CRT_PLL_PI1,  0),		// DQS2
	DPI_PHASE(DPI_DLL_CRT_PLL_PI1,  8),		// DQS3
	DPI_PHASE(DPI_DLL_CRT_PLL_PI1, 16),		// DQ0
	DPI_PHASE(DPI_DLL_CRT_PLL_PI2,  0),		// DQ1
	DPI_PHASE(DPI_DLL_CRT_PLL_PI2,  8),		// DQ2
	DPI_PHASE(DPI_DLL_CRT_PLL_PI2, 16),		// DQ3
};

static void dpi_tx_phase_tune_one(struct dpi_tx_phase_priv *priv,
				  struct dpi_tx_phase_data *p, int v)
{
	uint32_t val_ori, val;

	dpi_reg_read(priv->dpi, p->ofs, &val_ori);
	val = (val_ori >> p->sft) & 0x1f;
	val = (val + v) & 0x1f;
	val_ori = (val_ori & ~((0x1f << p->sft))) | (val << p->sft);
	dpi_reg_write(priv->dpi, p->ofs, val_ori);
}

static void dpi_tx_phase_tune(struct dpi_tx_phase_priv *priv, int32_t *pi_val_q, int state)
{
	int i, j, k, ofs, dir, val;

	dpi_clock_gating_disable(priv->dpi);

	for(i = 0; i < priv->num_q; ++i) {
		if(priv->ddr_type_q[i] == priv->dpi->type) {
			ofs = (pi_val_q[i] < 0) ? -pi_val_q[i] : pi_val_q[i];
			dir = (pi_val_q[i] < 0) ? -1 : 1;
			val = dir * state;

			pr_info("%s: state = %d, pi_idx = 0x%x, ofs= %d, dir = %d, val = %d\n", __func__, state, priv->pi_idx_q[i], ofs, dir, val);

			for (j = 0; j < ofs; j++) {
				for (k = 0; k < 15; k++) {
					if(priv->pi_idx_q[i] & (0x1 << k)) {
							dpi_tx_phase_tune_one(priv, &dpi_tx_phase[k], val);
					}
				}
			}
		}
	}

	dpi_clock_gating_enable(priv->dpi);

	pr_info("%s: CRT_PLL_PI0 = %08x\n", __func__, readl(priv->dpi->base + DPI_DLL_CRT_PLL_PI0));
	pr_info("%s: CRT_PLL_PI1 = %08x\n", __func__, readl(priv->dpi->base + DPI_DLL_CRT_PLL_PI1));
	pr_info("%s: CRT_PLL_PI2 = %08x\n", __func__, readl(priv->dpi->base + DPI_DLL_CRT_PLL_PI2));
	pr_info("%s: CRT_PLL_PI3 = %08x\n", __func__, readl(priv->dpi->base + DPI_DLL_CRT_PLL_PI3));
}

static int dpi_tx_phase_cb(struct notifier_block *nb, unsigned long event,
			   void *data)
{
	struct dpi_tx_phase_priv *priv = container_of(nb, struct dpi_tx_phase_priv, nb);
	int new_state;

	switch (event) {
	case DPI_EVENT_TEMP_STATUS_HOT:
	case DPI_EVENT_TEMP_STATUS_COLD:
		new_state = DPI_TPT_STATE_1;
		break;
	case DPI_EVENT_TEMP_STATUS_NORMAL:
		new_state = DPI_TPT_STATE_0;
		break;
	default:
		return NOTIFY_DONE;
	}

	pr_debug("%s: event = %ld, priv->state = %d, new_state = %d\n", __func__, event, priv->state, new_state);

	if (new_state != priv->state) {
		dpi_tx_phase_tune(priv, priv->pi_val_q_1, new_state);
	}
	priv->state = new_state;
	return NOTIFY_OK;
}

static int dpi_tx_phase_read_reg_values(struct device *dev, const char *name, uint32_t **val, uint32_t *num)
{
	struct device_node *np = dev->of_node;
	int len;

	if (!of_find_property(np, name, &len))
		return -EINVAL;

	len /= 4;

	*val = devm_kcalloc(dev, len, sizeof(**val), GFP_KERNEL);
	if(!(*val)) {
		dev_err(dev, "%s %s(): memory allocation failure\n", __FILE__, __func__);
		return -ENOMEM;
	}

	*num = len;

	return of_property_read_u32_array(np, name, *val, len);
}

static int dpi_tx_phase_of_init(struct device *dev, struct dpi_tx_phase_priv *priv)
{
	int ret, len;
	unsigned int *res;

	if ((ret = dpi_tx_phase_read_reg_values(dev, "ddr-type", &priv->ddr_type_q, &len)))
		return ret;

	if ((ret = dpi_tx_phase_read_reg_values(dev, "pi-sel-idx", &priv->pi_idx_q, &len)))
		return ret;

	ret = dpi_tx_phase_read_reg_values(dev, "state-0-value", &res, &len);
	if (ret)
		return ret;
	priv->pi_val_q_0 = res;

	ret = dpi_tx_phase_read_reg_values(dev, "state-1-value", &res, &len);
	if (ret)
		return ret;

	priv->pi_val_q_1 = res;
	priv->num_q = len;

	return 0;
}

static int dpi_tx_phase_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dpi_device *dpi = dev_get_drvdata(dev->parent);
	struct dpi_tx_phase_priv *priv;
	int ret;

	if (!dpi)
		return -EPROBE_DEFER;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dpi = dpi;
	priv->dev = dev;
	priv->state = DPI_TPT_STATE_0;
	priv->nb.notifier_call = dpi_tx_phase_cb;

	ret = dpi_tx_phase_of_init(dev, priv);
	if (ret) {
		dev_err(dev, "failed to parse data from dt: %d\n", ret);
		return ret;
	}

	dpi_tx_phase_tune(priv, priv->pi_val_q_0, 1);

	return dpi_register_notifier(dpi, &priv->nb);
}

static const struct of_device_id dpi_tx_phase_of_match[] = {
	{ .compatible = "realtek,dpi-tx-phase" },
	{}
};

static struct platform_driver dpi_tx_phase_driver = {
	.driver = {
		.name           = "rtk-dpi-tx-phase",
		.owner          = THIS_MODULE,
		.of_match_table = of_match_ptr(dpi_tx_phase_of_match),
	},
	.probe    = dpi_tx_phase_probe,
};
module_platform_driver(dpi_tx_phase_driver);

MODULE_LICENSE("GPL v2");
