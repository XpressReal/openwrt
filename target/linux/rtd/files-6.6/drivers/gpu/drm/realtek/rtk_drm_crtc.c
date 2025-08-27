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

#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_print.h>
#include <drm/drm_vblank.h>

#include <linux/platform_device.h>
#include <linux/component.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include <linux/of_device.h>
#include <linux/hwspinlock.h>
#include <linux/of_reserved_mem.h>

#include "rtk_drm_drv.h"
#include "rtk_drm_crtc.h"

#define to_rtk_crtc_state(s) container_of(s, struct rtk_crtc_state, base)

static const struct crtc_plane_data rtd_crtc_plane_main[] = {
	{ .layer_nr = VO_VIDEO_PLANE_OSD1,
	  .type = DRM_PLANE_TYPE_PRIMARY },
	{ .layer_nr = VO_VIDEO_PLANE_V1,
	  .type = DRM_PLANE_TYPE_OVERLAY },
};

static const struct crtc_plane_data rtd_crtc_plane_second[] = {
	{ .layer_nr = VO_VIDEO_PLANE_SUB1,
	  .type = DRM_PLANE_TYPE_PRIMARY },
	{ .layer_nr = VO_VIDEO_PLANE_V2,
	  .type = DRM_PLANE_TYPE_OVERLAY },
};

static const struct crtc_plane_data rtd_crtc_plane_extend[] = {
	{ .layer_nr = VO_VIDEO_PLANE_OSD2,
	  .type = DRM_PLANE_TYPE_PRIMARY },
};

static const struct crtc_data rtd_crtc_main = {
	.version = 0,
	.plane = rtd_crtc_plane_main,
	.plane_size = ARRAY_SIZE(rtd_crtc_plane_main),
	.mixer = 0,
};

static const struct crtc_data rtd_crtc_second = {
	.version = 0,
	.plane = rtd_crtc_plane_second,
	.plane_size = ARRAY_SIZE(rtd_crtc_plane_second),
	.mixer = 1,
};

static const struct crtc_data rtd_crtc_extend = {
	.version = 0,
	.plane = rtd_crtc_plane_extend,
	.plane_size = ARRAY_SIZE(rtd_crtc_plane_extend),
	.mixer = 2,
};

static int rtk_crtc_set_mixer_order(struct rtk_rpc_info *rpc_info,
				struct rpc_disp_mixer_order *mixer_order)
{
	int ret = 0;

	if(!rpc_info)
		return -1;

	ret = rpc_set_mixer_order(rpc_info, mixer_order);

	return ret;
}

static struct drm_crtc_state *rtk_crtc_duplicate_state(struct drm_crtc *crtc)
{
	struct rtk_crtc_state *state;

	if (WARN_ON(!crtc->state))
		return NULL;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;

	__drm_atomic_helper_crtc_duplicate_state(crtc, &state->base);

	WARN_ON(state->base.crtc != crtc);
	state->base.crtc = crtc;

	return &state->base;
}

static void rtk_crtc_destroy_state(struct drm_crtc *crtc,
				   struct drm_crtc_state *state)
{
	__drm_atomic_helper_crtc_destroy_state(state);
	kfree(to_rtk_crtc_state(state));
}

static int rtk_crtc_atomic_get_property(struct drm_crtc *crtc,
					const struct drm_crtc_state *state,
					struct drm_property *property,
					uint64_t *val)
{
	struct rtk_drm_crtc *rtk_crtc = to_rtk_crtc(crtc);

	if (property == rtk_crtc->present_time_prop) {
		*val = rtk_crtc->present_time;
		return 0;
	}
	return 0;
}

static int rtk_crtc_atomic_set_property(struct drm_crtc *crtc,
					struct drm_crtc_state *state,
					struct drm_property *property,
					uint64_t val)
{
	struct rtk_drm_crtc *rtk_crtc = to_rtk_crtc(crtc);
	const struct drm_display_mode *mode = &state->mode;
	u64 tolerance;

	if (property == rtk_crtc->present_time_prop) {
		tolerance = (u64)((mode->htotal * mode->vtotal)/(mode->clock)) * 500000;
		rtk_crtc->present_time = val - tolerance;
		rtk_crtc->present_time_en = 1;
		return 0;
	}
	return 0;
}

static int rtk_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct rtk_drm_crtc *rtk_crtc = to_rtk_crtc(crtc);
	struct rtk_rpc_info *rpc_info = rtk_crtc->rpc_info;
	unsigned long flags;
	unsigned int val;
	unsigned int notify;

	DRM_DEBUG_KMS("%d\n", __LINE__);

	if (rtk_crtc->hwlock)
		hwspin_lock_timeout_irqsave(rtk_crtc->hwlock, UINT_MAX, &flags);

	notify = DC_VO_SET_NOTIFY << (rtk_crtc->mixer * 2);

	val = readl(rpc_info->vo_sync_flag);
	val |= __cpu_to_be32(notify);
	writel(val, rpc_info->vo_sync_flag);

	if (rtk_crtc->hwlock)
		hwspin_unlock_irqrestore(rtk_crtc->hwlock, &flags);

	return 0;
}

static void rtk_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct rtk_drm_crtc *rtk_crtc = to_rtk_crtc(crtc);
	struct rtk_rpc_info *rpc_info = rtk_crtc->rpc_info;
	unsigned long flags;
	unsigned int val;
	unsigned int notify;

	DRM_DEBUG_KMS("%d\n", __LINE__);

	if (rtk_crtc->hwlock)
		hwspin_lock_timeout_irqsave(rtk_crtc->hwlock, UINT_MAX, &flags);

	notify = DC_VO_SET_NOTIFY << (rtk_crtc->mixer * 2);

	val = readl(rpc_info->vo_sync_flag);
	val &= ~(__cpu_to_be32(notify));
	writel(val, rpc_info->vo_sync_flag);

	if (rtk_crtc->hwlock)
		hwspin_unlock_irqrestore(rtk_crtc->hwlock, &flags);
}

static void rtk_crtc_destroy(struct drm_crtc *crtc)
{
	drm_crtc_cleanup(crtc);
}

void rtk_crtc_finish_page_flip(struct drm_crtc *crtc)
{
	struct rtk_drm_crtc *rtk_crtc = to_rtk_crtc(crtc);
	struct drm_device *drm = crtc->dev;
	unsigned long flags;

	spin_lock_irqsave(&drm->event_lock, flags);
	if (rtk_crtc->pending_needs_vblank) {
		drm_crtc_send_vblank_event(crtc, rtk_crtc->event);
		drm_crtc_vblank_put(crtc);
		rtk_crtc->event = NULL;
		rtk_crtc->pending_needs_vblank = false;
	}
	spin_unlock_irqrestore(&drm->event_lock, flags);
}

static const struct drm_crtc_funcs rtk_crtc_funcs = {
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.destroy = rtk_crtc_destroy,
	.reset = drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state = rtk_crtc_duplicate_state,
	.atomic_destroy_state = rtk_crtc_destroy_state,
	.atomic_set_property = rtk_crtc_atomic_set_property,
	.atomic_get_property = rtk_crtc_atomic_get_property,
	.enable_vblank = rtk_crtc_enable_vblank,
	.disable_vblank = rtk_crtc_disable_vblank,
};

static bool rtk_crtc_mode_fixup(struct drm_crtc *crtc,
				const struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	DRM_DEBUG_KMS("%d\n", __LINE__);
	return true;
}

static void rtk_crtc_atomic_flush(struct drm_crtc *crtc,
				struct drm_atomic_state *old_crtc_state)
{
	struct rtk_drm_crtc *rtk_crtc = to_rtk_crtc(crtc);

	DRM_DEBUG_KMS("%d\n", __LINE__);

	if (rtk_crtc->event)
		rtk_crtc->pending_needs_vblank = true;
}

static void rtk_crtc_atomic_begin(struct drm_crtc *crtc,
				struct drm_atomic_state *old_crtc_state)
{
	struct rtk_drm_crtc *rtk_crtc = to_rtk_crtc(crtc);
	struct rtk_crtc_state *state = to_rtk_crtc_state(crtc->state);

	DRM_DEBUG_KMS("%d\n", __LINE__);

	if (rtk_crtc->event && state->base.event)
		DRM_ERROR("new event while there is still a pending event\n");

	if (state->base.event) {
		WARN_ON(drm_crtc_vblank_get(crtc) != 0);
		rtk_crtc->event = state->base.event;
		state->base.event = NULL;
	}
}

static void rtk_crtc_atomic_enable(struct drm_crtc *crtc,
				struct drm_atomic_state *old_crtc_state)
{
	DRM_DEBUG_KMS("%d\n", __LINE__);
	drm_crtc_vblank_on(crtc);
}

static void rtk_crtc_atomic_disable(struct drm_crtc *crtc,
				struct drm_atomic_state *old_crtc_state)
{
	DRM_DEBUG_KMS("%d\n", __LINE__);

	drm_crtc_vblank_off(crtc);

	if (crtc->state->event) {
		spin_lock_irq(&crtc->dev->event_lock);
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		crtc->state->event = NULL;
		spin_unlock_irq(&crtc->dev->event_lock);
	}
}

static const struct drm_crtc_helper_funcs rtk_crtc_helper_funcs = {
	.mode_fixup = rtk_crtc_mode_fixup,
	.atomic_flush = rtk_crtc_atomic_flush,
	.atomic_begin = rtk_crtc_atomic_begin,
	.atomic_enable = rtk_crtc_atomic_enable,
	.atomic_disable = rtk_crtc_atomic_disable,
};

irqreturn_t rtk_crtc_isr(int irq, void *dev_id)
{
	struct rtk_drm_crtc *rtk_crtc = (struct rtk_drm_crtc *)dev_id;
	struct rtk_rpc_info *rpc_info = rtk_crtc->rpc_info;
	struct drm_crtc *crtc = &rtk_crtc->crtc;
	unsigned long flags;
	unsigned int feedback_notify;

	feedback_notify = __cpu_to_be32(1U << (rtk_crtc->mixer * 2 + 1));

	if (!DC_HAS_BIT(rpc_info->vo_sync_flag, feedback_notify))
		return IRQ_HANDLED;

	if (rtk_crtc->hwlock)
		hwspin_lock_timeout_irqsave(rtk_crtc->hwlock, UINT_MAX, &flags);

	DC_RESET_BIT(rpc_info->vo_sync_flag, feedback_notify);

	if (rtk_crtc->hwlock)
		hwspin_unlock_irqrestore(rtk_crtc->hwlock, &flags);

	drm_crtc_handle_vblank(crtc);

	if (rtk_plane_check_update_done(&rtk_crtc->nplanes[0])) {
		rtk_crtc_finish_page_flip(crtc);
	}

	return IRQ_HANDLED;
}

static void rtk_crtc_change_tv_system(struct drm_crtc *crtc)
{
	struct rtk_drm_crtc *rtk_crtc = to_rtk_crtc(crtc);
	for (int i = 0; i < rtk_crtc->plane_count; i++) {
		struct rtk_drm_plane *rtk_plane = &rtk_crtc->nplanes[i];

		rtk_plane->disp_win.videoWin.x = 0;
		rtk_plane->disp_win.videoWin.y = 0;
		rtk_plane->disp_win.videoWin.width = 0;
		rtk_plane->disp_win.videoWin.height = 0;
	}
	return;
}

static int rtk_crtc_bind(struct device *dev, struct device *master, void *data)
{
	struct device_node *np = dev->of_node;
	struct drm_device *drm = data;
	struct rtk_drm_private *priv = drm->dev_private;
	struct device_node *port;
	struct rtk_drm_crtc *rtk_crtc;
	const struct crtc_data *crtc_data;
	struct drm_plane *primary = NULL, *cursor = NULL;
	int lock_id;
	int i, ret;

	crtc_data = of_device_get_match_data(dev);
	if (!crtc_data)
		return -ENODEV;

	rtk_crtc = devm_kzalloc(dev, sizeof(*rtk_crtc), GFP_KERNEL);

	rtk_crtc->nplanes = devm_kcalloc(dev, crtc_data->plane_size,
					sizeof(struct rtk_drm_plane), GFP_KERNEL);
	if (!rtk_crtc->nplanes)
		return -ENOMEM;

	rtk_crtc->plane_count = crtc_data->plane_size;

	ret = of_reserved_mem_device_init(dev);
	if (ret)
		dev_warn(dev, "init reserved memory failed");

	rtk_crtc->rpc_info = &priv->rpc_info;
	dev_set_drvdata(dev, rtk_crtc);

	memset(&rtk_crtc->mixer_order, 0, sizeof(struct rpc_disp_mixer_order));

	for (i = 0; i < crtc_data->plane_size; i++) {
		const struct crtc_plane_data *plane = &crtc_data->plane[i];

		if (plane->type != DRM_PLANE_TYPE_PRIMARY &&
		    plane->type != DRM_PLANE_TYPE_CURSOR)
			continue;

		rtk_crtc->mixer = crtc_data->mixer;
		rtk_crtc->nplanes[i].mixer = rtk_crtc->mixer;

		rtk_plane_init(drm, &rtk_crtc->nplanes[i], 0, plane->type, plane->layer_nr);

		if (plane->type == DRM_PLANE_TYPE_PRIMARY) {
			rtk_crtc->mixer_order.osd1 = 2;
			primary = &rtk_crtc->nplanes[i].plane;
		} else {
			rtk_crtc->mixer_order.sub1 = 3;
			cursor = &rtk_crtc->nplanes[i].plane;
		}
	}

	drm_crtc_init_with_planes(drm, &rtk_crtc->crtc,
				  primary,
				  cursor,
				  &rtk_crtc_funcs, NULL);

	drm_crtc_helper_add(&rtk_crtc->crtc, &rtk_crtc_helper_funcs);

	for (i = 0; i < crtc_data->plane_size; i++) {
		const struct crtc_plane_data *plane = &crtc_data->plane[i];

		if (plane->type != DRM_PLANE_TYPE_OVERLAY)
			continue;

		rtk_crtc->mixer = crtc_data->mixer;
		rtk_crtc->nplanes[i].mixer = rtk_crtc->mixer;

		rtk_plane_init(drm, &rtk_crtc->nplanes[i],
				1 << drm_crtc_index(&rtk_crtc->crtc),
				plane->type, plane->layer_nr);

		if (i == 2)
			rtk_crtc->mixer_order.v1 = 1;
		else
			rtk_crtc->mixer_order.v2 = 0;
	}

	if (rtk_crtc_set_mixer_order(rtk_crtc->rpc_info, &rtk_crtc->mixer_order)) {
		DRM_ERROR("rtk crtc set mixer order fail\n");
		return -1;
	}

	port = of_get_child_by_name(dev->of_node, "port");
	if (!port)
		DRM_ERROR("no connect port node found\n");

	rtk_crtc->crtc.port = port;

	rtk_crtc->irq = irq_of_parse_and_map(np, 0);
	if (!rtk_crtc->irq) {
		DRM_ERROR("no irq for crtc\n");
		return -1;
	}

	if (devm_request_irq(dev, rtk_crtc->irq, rtk_crtc_isr, IRQF_SHARED | IRQF_NO_SUSPEND,
			  "crtc_irq", rtk_crtc)) {
		DRM_ERROR("can't request crtc irq\n");
		return -1;
	}

	rtk_crtc->change_tv_system = rtk_crtc_change_tv_system;

	lock_id = of_hwspin_lock_get_id(dev->of_node, 0);
	if (lock_id > 0 || (IS_ENABLED(CONFIG_HWSPINLOCK) && lock_id == 0)) {
		struct hwspinlock *lock = devm_hwspin_lock_request_specific(dev, lock_id);

		if (lock) {
			dev_info(dev, "use hwlock%d\n", lock_id);
			rtk_crtc->hwlock = lock;
		}
	} else {
		if (lock_id != -ENOENT)
			dev_err(dev, "failed to get hwlock: %pe\n", ERR_PTR(lock_id));
	}

	rtk_crtc->present_time_prop = drm_property_create_range(drm, DRM_MODE_PROP_ATOMIC,
					"expectedPresentTime", 0, 0xffffffffffffffff);
	drm_object_attach_property(&rtk_crtc->crtc.base, rtk_crtc->present_time_prop, 0);

	return 0;
}

static void
rtk_crtc_unbind(struct device *dev, struct device *master, void *data)
{
	struct rtk_drm_crtc *rtk_crtc = dev_get_drvdata(dev);
	struct drm_device *drm = rtk_crtc->crtc.dev;
	struct drm_plane *plane, *tmp;

	list_for_each_entry_safe(plane, tmp, &drm->mode_config.plane_list, head)
		rtk_plane_destroy(plane);

	drm_crtc_cleanup(&rtk_crtc->crtc);
}

const struct component_ops rtk_crtc_component_ops = {
	.bind = rtk_crtc_bind,
	.unbind = rtk_crtc_unbind,
};

static int rtk_crtc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	return component_add(dev, &rtk_crtc_component_ops);
}

static int rtk_crtc_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &rtk_crtc_component_ops);
	return 0;
}

static const struct of_device_id rtk_crtc_of_ids[] = {
	{ .compatible = "realtek,rtd-crtc-main",
	  .data = &rtd_crtc_main },
	{ .compatible = "realtek,rtd-crtc-second",
	  .data = &rtd_crtc_second },
	{ .compatible = "realtek,rtd-crtc-extend",
	  .data = &rtd_crtc_extend },
	{},
};

struct platform_driver rtk_crtc_platform_driver = {
	.probe = rtk_crtc_probe,
	.remove = rtk_crtc_remove,
	.driver = {
		.name = "realtek-crtc",
		.of_match_table = rtk_crtc_of_ids,
	},
};
