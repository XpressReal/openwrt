// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2019 Realtek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <drm/drm_of.h>
#include <drm/drm_print.h>

#include <linux/component.h>
#include <linux/platform_device.h>

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/reset.h>
#include <linux/delay.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#include <linux/module.h>
#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>
#include <drm/drm_modes.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_probe_helper.h>

#include "rtk_drm_drv.h"
#include "rtk_dsi_reg.h"
#include "rtk_dsi.h"

ssize_t rtk_dsi_enable_pattern_gen(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
static DEVICE_ATTR(enable_dsi_pattern_gen, S_IWUSR, NULL, rtk_dsi_enable_pattern_gen);

#define to_rtk_dsi(x) container_of(x, struct rtk_dsi, x)

static const char *mixer_names[] = {
	"MIXER1",
	"MIXER2",
	"MIXER3",
	"MIXER_NONE",
	"MIXER_INVALID",
};

static const char *interface_names[] = {
	"DisplayPort",
	"e DisplayPort",
	"MIPI DSI",
	"LVDS1",
	"LVDS2",
	"CVBS",
};

struct interface_info {
	unsigned int display_interface;
	unsigned int width;
	unsigned int height;
	unsigned int frame_rate;
	unsigned int mixer;
};

struct rtk_dsi {
	struct drm_device *drm_dev;
	struct drm_panel *panel;

	struct drm_connector connector;
	struct drm_encoder encoder;
	struct mipi_dsi_host host;
	struct regmap *reg;
	struct clk *clk;
	struct reset_control *rstc;
	struct rtk_rpc_info *rpc_info;
	enum dsi_fmt fmt;
	enum mipi_dsi_pixel_format format;
	unsigned int swap_enable;
};

static void rtk_dsi_init(struct rtk_dsi *dsi, struct drm_display_mode *mode)
{
	unsigned int reg;

	regmap_write(dsi->reg, PAT_GEN, 0xf000000);

	if (dsi->fmt == DSI_FMT_720P_60 ||
		dsi->fmt == DSI_FMT_1080P_60 ||
		dsi->fmt == DSI_FMT_1200_1920P_60 ||
		dsi->fmt == DSI_FMT_600_1024P_60 ||
		dsi->fmt == DSI_FMT_800_480P_60) {
		regmap_write(dsi->reg, CLOCK_GEN, 0x3f4000);
	} else
		regmap_write(dsi->reg, CLOCK_GEN, 0x7f4000);

	regmap_write(dsi->reg, WATCHDOG, 0x1632);
	regmap_write(dsi->reg, CTRL_REG, 0x7000000);
	regmap_write(dsi->reg, DF, 0x1927c20);

	if (dsi->fmt == DSI_FMT_720P_60 ||
		dsi->fmt == DSI_FMT_600_1024P_60) {
		regmap_write(dsi->reg, SSC2, 0x4260426);
		regmap_write(dsi->reg, SSC3, 0x280F0F);
	} else if (dsi->fmt == DSI_FMT_800_480P_60) {
		regmap_write(dsi->reg, SSC2, 0x5870587);
		regmap_write(dsi->reg, SSC3, 0x281515);
	} else {
		regmap_write(dsi->reg, SSC2, 0x4c05ed);
		regmap_write(dsi->reg, SSC3, 0x282219);
	}

	regmap_write(dsi->reg, MPLL, 0x403592b);

	if (dsi->fmt == DSI_FMT_600_1024P_60) {
		regmap_write(dsi->reg, TX_DATA1, 0x20d0100);
		regmap_write(dsi->reg, TX_DATA2, 0x81d020f);
		regmap_write(dsi->reg, TX_DATA3, 0x5091402);
	} else {
		regmap_write(dsi->reg, TX_DATA1, 0x70d0100);
		regmap_write(dsi->reg, TX_DATA2, 0x81d090f);
		regmap_write(dsi->reg, TX_DATA3, 0x5091408);
	}

	regmap_read(dsi->reg, CLOCK_GEN, &reg);

	if (dsi->fmt == DSI_FMT_800_480P_60)
		reg |= 0x710;
	else
		reg |= 0x7f0;
	regmap_write(dsi->reg, CLOCK_GEN, reg);

	regmap_write(dsi->reg, DF, 0x1927c3c);
	regmap_write(dsi->reg, WATCHDOG, 0x161a);

	regmap_write(dsi->reg, CLK_CONTINUE, 0x80);

	if (dsi->format == MIPI_DSI_FMT_RGB888)
		regmap_write(dsi->reg, TC0,
			(mode->htotal - mode->hsync_end) << 16 | mode->hdisplay * 3);
	else if (dsi->format == MIPI_DSI_FMT_RGB565)
		regmap_write(dsi->reg, TC0,
			(mode->htotal - mode->hsync_end) << 16 | mode->hdisplay * 2);
	else if (dsi->format == MIPI_DSI_FMT_RGB666)
		regmap_write(dsi->reg, TC0,
			(mode->htotal - mode->hsync_end) << 16 | mode->hdisplay * 225 / 100);

	regmap_write(dsi->reg, TC2,
		(mode->vtotal - mode->vsync_end) << 16 | mode->vdisplay);
	regmap_write(dsi->reg, TC1,
		(mode->hsync_end - mode->hsync_start) << 16 | (mode->hsync_start - mode->hdisplay));
	regmap_write(dsi->reg, TC3,
		(mode->vsync_end - mode->vsync_start) << 16 | (mode->vsync_start - mode->vdisplay));

	if (dsi->fmt == DSI_FMT_720P_60) {
		regmap_write(dsi->reg, TC5, 0x500056d);
		regmap_write(dsi->reg, TC4, 0x241032b);
	} else if (dsi->fmt == DSI_FMT_600_1024P_60) {
		regmap_write(dsi->reg, TC5, 0x258037f);
		regmap_write(dsi->reg, TC4, 0x24101bf);

		regmap_write(dsi->reg, TO1, 0xffff);
		regmap_write(dsi->reg, TO2, 0xffff);
	} else if (dsi->fmt == DSI_FMT_800_480P_60) {
		regmap_write(dsi->reg, TC5, 0x3200a90);
		regmap_write(dsi->reg, TC4, 0x2410552);

		regmap_write(dsi->reg, TO1, 0xffff);
		regmap_write(dsi->reg, TO2, 0xffff);

		regmap_write(dsi->reg, CTRL_REG, 0x7010000);
	} else if (dsi->fmt == DSI_FMT_1080P_60) {
		regmap_write(dsi->reg, TC5, 0x780073c);
		regmap_write(dsi->reg, TC4, 0x241032b);
	} else if (dsi->fmt == DSI_FMT_1200_1920P_60) {
		regmap_write(dsi->reg, TC5, 0x4b0041b);
		regmap_write(dsi->reg, TC4, 0x241032b);
 	} else {
		regmap_write(dsi->reg, TC4, 0x241032b);
	}

	if (dsi->swap_enable)
		regmap_write(dsi->reg, TX_SWAP, 0x00343210); //TX: D0,D1 P/N swap, for swap tranfer
}

static void rtk_dsi_enc_mode_set(struct drm_encoder *encoder,
				 struct drm_display_mode *mode,
				 struct drm_display_mode *adj_mode)
{
	struct rtk_dsi *dsi = to_rtk_dsi(encoder);

	if (adj_mode->hdisplay == 1280 &&
		adj_mode->vdisplay == 720 &&
		drm_mode_vrefresh(adj_mode) == 60) {
		dsi->fmt = DSI_FMT_720P_60;
	} else if (adj_mode->hdisplay == 1920 &&
		adj_mode->vdisplay == 1080 &&
		drm_mode_vrefresh(adj_mode) == 60) {
		dsi->fmt = DSI_FMT_1080P_60;
	} else if (adj_mode->hdisplay == 1200 &&
		adj_mode->vdisplay == 1920 &&
		drm_mode_vrefresh(adj_mode) == 60) {
		dsi->fmt = DSI_FMT_1200_1920P_60;
	} else if (adj_mode->hdisplay == 800 &&
		adj_mode->vdisplay == 1280 &&
		drm_mode_vrefresh(adj_mode) == 60) {
		dsi->fmt = DSI_FMT_800_1280P_60;
	} else if (adj_mode->hdisplay == 600 &&
		adj_mode->vdisplay == 1024 &&
		drm_mode_vrefresh(adj_mode) == 60) {
		dsi->fmt = DSI_FMT_600_1024P_60;
	} else if (adj_mode->hdisplay == 1920 &&
		adj_mode->vdisplay == 720 &&
		drm_mode_vrefresh(adj_mode) == 60) {
		dsi->fmt = DSI_FMT_1920_720P_60;
	} else if (adj_mode->hdisplay == 1920 &&
		adj_mode->vdisplay == 720 &&
		drm_mode_vrefresh(adj_mode) == 30) {
		dsi->fmt = DSI_FMT_1920_720P_30;
	} else if (adj_mode->hdisplay == 600 &&
		adj_mode->vdisplay == 1024 &&
		drm_mode_vrefresh(adj_mode) == 30) {
		dsi->fmt = DSI_FMT_600_1024P_30;
	} else if (adj_mode->hdisplay == 800 &&
		adj_mode->vdisplay == 480 &&
		drm_mode_vrefresh(adj_mode) == 60) {
		dsi->fmt = DSI_FMT_800_480P_60;
	}

	return;
}

static void rtk_dsi_enc_enable(struct drm_encoder *encoder)
{
	struct drm_display_mode *mode = &encoder->crtc->state->adjusted_mode;
	struct rtk_dsi *dsi = to_rtk_dsi(encoder);
	struct rtk_rpc_info *rpc_info = dsi->rpc_info;
	struct rpc_set_display_out_interface interface;
	int ret;

	ret = clk_prepare_enable(dsi->clk);
	if (ret) {
		DRM_ERROR("Failed to enable clk: %d\n", ret);
	}

	reset_control_deassert(dsi->rstc);

	rtk_dsi_init(dsi, mode);

	if (dsi->panel)
		drm_panel_prepare(dsi->panel);

	if (dsi->fmt == DSI_FMT_800_480P_60)
		regmap_write(dsi->reg, CTRL_REG, 0x7610001);
	else
		regmap_write(dsi->reg, CTRL_REG, 0x7610031);

	regmap_write(dsi->reg, PAT_GEN, 0x9000000);

	interface.display_interface       = DISPLAY_INTERFACE_MIPI;
	interface.width                   = mode->hdisplay;
	interface.height                  = mode->vdisplay;
	interface.frame_rate              = drm_mode_vrefresh(mode);
	interface.display_interface_mixer = DISPLAY_INTERFACE_MIXER2;

	printk(KERN_ALERT"mode->hdisplay : %d, mode->vdisplay : %d, frame_rate : %d\n",
		mode->hdisplay, mode->vdisplay, drm_mode_vrefresh(mode));

	printk(KERN_ALERT"enable %s on %s\n",
		interface_names[interface.display_interface], mixer_names[interface.display_interface_mixer]);

	ret = rpc_set_out_interface(rpc_info, &interface);
	if (ret) {
		DRM_ERROR("rpc_set_out_interface rpc fail\n");
	}

	if (dsi->panel)
		drm_panel_enable(dsi->panel);
}

static void rtk_dsi_enc_disable(struct drm_encoder *encoder)
{
	struct drm_display_mode *mode = &encoder->crtc->state->adjusted_mode;
	struct rtk_dsi *dsi = to_rtk_dsi(encoder);
	struct rtk_rpc_info *rpc_info = dsi->rpc_info;
	struct rpc_set_display_out_interface interface;
	int ret;

	interface.display_interface       = DISPLAY_INTERFACE_MIPI;
	interface.width                   = mode->hdisplay;
	interface.height                  = mode->vdisplay;
	interface.frame_rate              = drm_mode_vrefresh(mode);
	interface.display_interface_mixer = DISPLAY_INTERFACE_MIXER_NONE;

	if (dsi->panel) {
		drm_panel_disable(dsi->panel);
		drm_panel_unprepare(dsi->panel);
	}

	regmap_write(dsi->reg, PAT_GEN, 0x9000000);

	printk(KERN_ALERT"disable %s\n",
		interface_names[interface.display_interface]);

	ret = rpc_set_out_interface(rpc_info, &interface);
	if (ret) {
		DRM_ERROR("rpc_set_out_interface rpc fail\n");
	}

	clk_disable_unprepare(dsi->clk);
	reset_control_assert(dsi->rstc);
}

static int rtk_dsi_enc_atomic_check(struct drm_encoder *encoder,
				struct drm_crtc_state *crtc_state,
				struct drm_connector_state *conn_state)
{
	return 0;
}

struct regmap *dsi_reg = NULL;

ssize_t rtk_dsi_enable_pattern_gen(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long state;
	int ret, enable, pat_gen;

	ret = kstrtol(buf, 0, &state);
	/* valid input value: 0x8-0xf */
	enable = state & (DSI_PAT_GEN_MAX);

	if (!enable) {
		regmap_write(dsi_reg, PAT_GEN, 0x0);
		return count;
	}

	pat_gen = state & (0x7);

	switch (pat_gen) {
	case DSI_PAT_GEN_COLORBAR:
		regmap_write(dsi_reg, PAT_GEN, 0x8000000);
		break;
	case DSI_PAT_GEN_BLACK:
		regmap_write(dsi_reg, PAT_GEN, 0x9000000);
		break;
	case DSI_PAT_GEN_WHITE:
		regmap_write(dsi_reg, PAT_GEN, 0xa000000);
		break;
	case DSI_PAT_GEN_RED:
		regmap_write(dsi_reg, PAT_GEN, 0xb000000);
		break;
	case DSI_PAT_GEN_BLUE:
		regmap_write(dsi_reg, PAT_GEN, 0xc000000);
		break;
	case DSI_PAT_GEN_YELLOW:
		regmap_write(dsi_reg, PAT_GEN, 0xd000000);
		break;
	case DSI_PAT_GEN_MAGENTA:
		regmap_write(dsi_reg, PAT_GEN, 0xe000000);
		break;
	case DSI_PAT_GEN_USER_DEFINE:
		regmap_write(dsi_reg, PAT_GEN, 0xf000000);
		break;
	default:
		DRM_ERROR("Invalid argument\n");
		break;
	}

	return count;
}

static const struct drm_encoder_funcs rtk_dsi_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static const struct drm_encoder_helper_funcs rtk_dsi_encoder_helper_funcs = {
	.mode_set   = rtk_dsi_enc_mode_set,
	.enable     = rtk_dsi_enc_enable,
	.disable    = rtk_dsi_enc_disable,
	.atomic_check = rtk_dsi_enc_atomic_check,
};

static enum drm_connector_status rtk_dsi_conn_detect(
	struct drm_connector *connector, bool force)
{
	struct rtk_dsi *dsi = to_rtk_dsi(connector);

	DRM_DEBUG_KMS("%s\n", __func__);

	return dsi->panel ? connector_status_connected :
			    connector_status_disconnected;
}

static void rtk_dsi_conn_destroy(struct drm_connector *connector)
{
	DRM_DEBUG_KMS("%s\n", __func__);

	drm_connector_cleanup(connector);
}

static int rtk_dsi_conn_get_modes(struct drm_connector *connector)
{
	struct rtk_dsi *dsi = to_rtk_dsi(connector);

	return drm_panel_get_modes(dsi->panel, connector);
}

static enum drm_mode_status rtk_dsi_conn_mode_valid(
	struct drm_connector *connector, struct drm_display_mode *mode)
{
	DRM_DEBUG_KMS("%s\n", __func__);

	return MODE_OK;
}

static const struct drm_connector_funcs rtk_dsi_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = rtk_dsi_conn_detect,
	.destroy = rtk_dsi_conn_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static struct drm_connector_helper_funcs rtk_dsi_connector_helper_funcs = {
	.get_modes = rtk_dsi_conn_get_modes,
	.mode_valid = rtk_dsi_conn_mode_valid,
};

static int rtk_dsi_bind(struct device *dev, struct device *master,
				 void *data)
{
	struct drm_device *drm = data;
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	struct rtk_drm_private *priv = drm->dev_private;
	struct rtk_dsi *dsi = dev_get_drvdata(dev);
	int ret;
	int err = 0;

	dsi->clk = devm_clk_get(dev, "clk_en_dsi");

	if (IS_ERR(dsi->clk)) {
		dev_err(dev, "failed to get clock\n");
		return PTR_ERR(dsi->clk);
	}

	ret = clk_prepare_enable(dsi->clk);
	if (ret) {
		DRM_ERROR("Failed to enable clk: %d\n", ret);
		return ret;
	}

	dsi->rstc = devm_reset_control_get(dev, "dsi");
	if (IS_ERR(dsi->rstc)) {
		dev_err(dev, "failed to get reset controller\n");
		return PTR_ERR(dsi->rstc);
	}
	reset_control_deassert(dsi->rstc);

	dsi->reg = syscon_regmap_lookup_by_phandle(dev->of_node, "syscon");

	if (IS_ERR(dsi->reg)) {
		return PTR_ERR(dsi->reg);
	}

	dsi_reg = dsi->reg;
	encoder = &dsi->encoder;
	encoder->possible_crtcs = drm_of_find_possible_crtcs(drm, dev->of_node);
	if (encoder->possible_crtcs == 0)
		return -EPROBE_DEFER;

	drm_encoder_init(drm, encoder, &rtk_dsi_encoder_funcs,
			 DRM_MODE_ENCODER_DSI, "rtk_dsi0");

	drm_encoder_helper_add(encoder, &rtk_dsi_encoder_helper_funcs);

	connector = &dsi->connector;
	drm_connector_init(drm, connector, &rtk_dsi_connector_funcs,
			   DRM_MODE_CONNECTOR_DSI);
	drm_connector_helper_add(connector, &rtk_dsi_connector_helper_funcs);

	drm_connector_attach_encoder(connector, encoder);

	dsi->rpc_info = &priv->rpc_info;

	err = device_create_file(drm->dev, &dev_attr_enable_dsi_pattern_gen);
	if (err < 0)
		DRM_ERROR("failed to create dsi pattern gen\n");

	return 0;
}

static void rtk_dsi_unbind(struct device *dev, struct device *master,
			     void *data)
{
	struct drm_device *drm = data;

	dsi_reg = NULL;

	device_remove_file(drm->dev, &dev_attr_enable_dsi_pattern_gen);
}

static const struct component_ops rtk_dsi_ops = {
	.bind	= rtk_dsi_bind,
	.unbind	= rtk_dsi_unbind,
};

static int rtk_dsi_host_attach(struct mipi_dsi_host *host,
			       struct mipi_dsi_device *device)
{
	struct rtk_dsi *dsi = to_rtk_dsi(host);
	struct drm_panel *panel;
	struct device *dev = host->dev;
	int ret;

	ret = drm_of_find_panel_or_bridge(dev->of_node, 1, 0, &panel, NULL);
	if (ret) {
		DRM_ERROR("Failed to find panel\n");
		return ret;
	}

	of_property_read_u32(dev->of_node, "swap", &dsi->swap_enable);

	dsi->panel = panel;
	dsi->format = device->format;

	return ret;
}

static int rtk_dsi_host_detach(struct mipi_dsi_host *host,
			       struct mipi_dsi_device *device)
{
	struct rtk_dsi *dsi = to_rtk_dsi(host);

	dsi->panel = NULL;

	return 0;
}

static ssize_t rtk_dsi_host_transfer(struct mipi_dsi_host *host,
				     const struct mipi_dsi_msg *msg)
{
	struct rtk_dsi *dsi = to_rtk_dsi(host);
	unsigned char *buf;
	unsigned int len;
	unsigned int cnt, data[2], lastbyte;
	int i, j, tmp, ret = -1;
	unsigned int cmd;

	buf = (unsigned char *)msg->tx_buf;
	len = msg->tx_len;

	DRM_DEBUG_DRIVER("msg->type : 0x%x\n", msg->type);

	regmap_write(dsi->reg, CTRL_REG, 0x7010000);

	cmd = msg->type;
	switch (msg->type) {
	case MIPI_DSI_DCS_SHORT_WRITE:
	case MIPI_DSI_DCS_SHORT_WRITE_PARAM:
	case MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM:
		tmp = (*(unsigned int *)buf << 8) | cmd;
		regmap_write(dsi->reg, CMD0, tmp);
		DRM_DEBUG_DRIVER("write CMD0 0x%x\n", tmp);
		break;
	case MIPI_DSI_GENERIC_LONG_WRITE:
	case MIPI_DSI_DCS_LONG_WRITE:
		DRM_DEBUG_DRIVER("cmd : %d, len : %d\n", cmd, len);

		cnt = (len>>3) + ((len%8)?1:0);
		for (i = 0; i < cnt; i++) {
			data[0] = data[1] = 0;
			if (i == cnt-1)
				lastbyte = len % 8;
			else
				lastbyte = 8;

			for (j = 0; j < lastbyte; j++)
				*((unsigned char *)(data) + j) = buf[i*8+j];

			DRM_DEBUG_DRIVER("write IDMA1 0x%x\n", data[0]);
			DRM_DEBUG_DRIVER("write IDMA2 0x%x\n", data[1]);
			DRM_DEBUG_DRIVER("write IDMA0 0x%x\n", 0x10000 | i);

			regmap_write(dsi->reg, IDMA1, data[0]);
			regmap_write(dsi->reg, IDMA2, data[1]);
			regmap_write(dsi->reg, IDMA0, (0x10000 | i));
		}

		DRM_DEBUG_DRIVER("write CMD0 0x%x\n", (cmd | len << 8));
		regmap_write(dsi->reg, CMD0, (cmd | len << 8));

		break;
	default:
		pr_err("not support yet\n");
		break;
	}

	cnt = 0;
	regmap_write(dsi->reg, CMD_GO, 0x1);
	while(1) {
		regmap_read(dsi->reg, INTS, &tmp);
		tmp = tmp & 0x4;
		if (tmp || cnt >= 10)
			break;
		msleep(10);
		cnt++;
	}
	if (cnt >= 10)
		dev_err(dsi->host.dev, "command fail\n");
	else
		ret = 0;

	regmap_write(dsi->reg, INTS, 0x4);

	return ret;
}

static const struct mipi_dsi_host_ops rtk_dsi_host_ops = {
	.attach = rtk_dsi_host_attach,
	.detach = rtk_dsi_host_detach,
	.transfer = rtk_dsi_host_transfer,
};

static int rtk_dsi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rtk_dsi *dsi;

	dsi = devm_kzalloc(dev, sizeof(*dsi), GFP_KERNEL);
	if (!dsi)
		return -ENOMEM;

	dev_set_drvdata(dev, dsi);

	dsi->host.ops = &rtk_dsi_host_ops;
	dsi->host.dev = dev;
	mipi_dsi_host_register(&dsi->host);

	return component_add(&pdev->dev, &rtk_dsi_ops);
}

static int rtk_dsi_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &rtk_dsi_ops);
	return 0;
}

static const struct of_device_id rtk_dsi_dt_ids[] = {
	{ .compatible = "realtek,rtk-dsi",
	},
	{},
};
MODULE_DEVICE_TABLE(of, rtk_dsi_dt_ids);

struct platform_driver rtk_dsi_driver = {
	.probe  = rtk_dsi_probe,
	.remove = rtk_dsi_remove,
	.driver = {
		.name = "rtk-dsi",
		.of_match_table = rtk_dsi_dt_ids,
	},
};
