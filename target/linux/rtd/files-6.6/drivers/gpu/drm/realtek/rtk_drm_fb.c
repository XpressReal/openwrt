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

#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem.h>

#include "rtk_drm_fb.h"
#include "rtk_drm_gem.h"
#include "rtk_drm_drv.h"
#include "rtk_hdmi.h"

#define to_rtk_drm_fb(x) container_of(x, struct rtk_drm_fb, fb)

struct rtk_drm_fb {
	struct drm_framebuffer fb;
	struct drm_gem_object *obj[RTK_MAX_FB_BUFFER];
};

struct drm_gem_object *rtk_fb_get_gem_obj(struct drm_framebuffer *fb,
					  unsigned int plane)
{
	struct rtk_drm_fb *rtk_fb = to_rtk_drm_fb(fb);

	if (plane >= RTK_MAX_FB_BUFFER)
		return NULL;

	return rtk_fb->obj[plane];
}

static void rtk_drm_fb_destroy(struct drm_framebuffer *fb)
{
	struct rtk_drm_fb *rtk_fb = to_rtk_drm_fb(fb);
	int i;

	for (i = 0; i < RTK_MAX_FB_BUFFER; i++)
		drm_gem_object_put(rtk_fb->obj[i]);

	drm_framebuffer_cleanup(fb);
	kfree(rtk_fb);
}

static int rtk_drm_fb_create_handle(struct drm_framebuffer *fb,
				    struct drm_file *file_priv,
				    unsigned int *handle)
{
	struct rtk_drm_fb *rtk_fb = to_rtk_drm_fb(fb);

	return drm_gem_handle_create(file_priv,
				     rtk_fb->obj[0], handle);
}

static int rtk_drm_fb_dirty(struct drm_framebuffer *fb,
			    struct drm_file *file,
			    unsigned int flags, unsigned int color,
			    struct drm_clip_rect *clips,
			    unsigned int num_clips)
{
	DRM_DEBUG_KMS("%d\n", __LINE__);
	return 0;
}

static const struct drm_framebuffer_funcs rtk_drm_fb_funcs = {
	.destroy	= rtk_drm_fb_destroy,
	.create_handle	= rtk_drm_fb_create_handle,
	.dirty		= rtk_drm_fb_dirty,
};

static struct rtk_drm_fb *
rtk_drm_fb_alloc(struct drm_device *dev,
		 const struct drm_mode_fb_cmd2 *mode_cmd,
		 struct drm_gem_object **obj, unsigned int num_planes)
{
	struct rtk_drm_fb *rtk_fb;
	int ret;
	int i;

	rtk_fb = kzalloc(sizeof(*rtk_fb), GFP_KERNEL);
	if (!rtk_fb)
		return ERR_PTR(-ENOMEM);

	drm_helper_mode_fill_fb_struct(dev, &rtk_fb->fb, mode_cmd);

	for (i = 0; i < num_planes; i++) {
		rtk_fb->obj[i] = obj[i];
		rtk_fb->fb.obj[i] = obj[i];
	}

	ret = drm_framebuffer_init(dev, &rtk_fb->fb,
				   &rtk_drm_fb_funcs);
	if (ret) {
		DRM_ERROR("Failed to initialize framebuffer: %d\n", ret);
		kfree(rtk_fb);
		return ERR_PTR(ret);
	}

	return rtk_fb;
}

static struct drm_framebuffer *
rtk_drm_fb_create(struct drm_device *dev, struct drm_file *file_priv,
		  const struct drm_mode_fb_cmd2 *mode_cmd)
{
	const struct drm_format_info *info =
				drm_get_format_info(dev, mode_cmd);
	struct rtk_drm_fb *rtk_fb;
	struct drm_gem_object *objs[RTK_MAX_FB_BUFFER];
	struct drm_gem_object *obj;
	struct rtk_gem_object *rtk_obj;
	int num_planes = min_t(int, info->num_planes, RTK_MAX_FB_BUFFER);
	int ret;
	int i;

	for (i = 0; i < num_planes; i++) {
		unsigned int width = mode_cmd->width / (i ? info->hsub : 1);
		unsigned int height = mode_cmd->height / (i ? info->vsub : 1);
		unsigned int min_size;

		obj = drm_gem_object_lookup(file_priv, mode_cmd->handles[i]);
		if (!obj) {
			DRM_ERROR("Failed to lookup GEM object\n");
			ret = -ENXIO;
			goto err_gem_object_unreference;
		}

		rtk_obj = to_rtk_gem_obj(obj);
		min_size = (height - 1) * mode_cmd->pitches[i] +
			mode_cmd->offsets[i] +
			width * info->cpp[i];

		if (obj->size < min_size && rtk_obj->dmabuf_type == DMABUF_TYPE_NORMAL) {
			drm_gem_object_put(obj);
			ret = -EINVAL;
			goto err_gem_object_unreference;
		}
		objs[i] = obj;
	}

	rtk_fb = rtk_drm_fb_alloc(dev, mode_cmd, objs, i);
	if (IS_ERR(rtk_fb)) {
		ret = PTR_ERR(rtk_fb);
		goto err_gem_object_unreference;
	}

	return &rtk_fb->fb;

err_gem_object_unreference:
	for (i--; i >= 0; i--)
		drm_gem_object_put(objs[i]);
	return ERR_PTR(ret);
}

static void rtk_drm_output_poll_changed(struct drm_device *dev)
{
	DRM_DEBUG_KMS("%d\n", __LINE__);
}

static void rtk_drm_atomic_commit_tail(struct drm_atomic_state *state)
{
	struct drm_connector *connector;
	struct drm_connector_state *old_con_state, *new_con_state;
	uint32_t i;
	struct drm_device *dev = state->dev;

	drm_atomic_helper_commit_modeset_disables(dev, state);

	drm_atomic_helper_commit_modeset_enables(dev, state);

	drm_atomic_helper_commit_planes(dev, state, DRM_PLANE_COMMIT_ACTIVE_ONLY);

	drm_atomic_helper_fake_vblank(state);

	drm_atomic_helper_commit_hw_done(state);

	drm_atomic_helper_wait_for_vblanks(dev, state);

	drm_atomic_helper_cleanup_planes(dev, state);

	for_each_oldnew_connector_in_state(state, connector, old_con_state, new_con_state, i) {
		if (connector->connector_type == DRM_MODE_CONNECTOR_HDMIA)
			rtk_hdcp_commit_state(connector,
				old_con_state->content_protection,
				new_con_state->content_protection);
	}
}

static struct drm_mode_config_helper_funcs rtk_drm_mode_config_helpers = {
	.atomic_commit_tail = rtk_drm_atomic_commit_tail,
};

static const struct drm_mode_config_funcs rtk_drm_mode_config_funcs = {
	.fb_create = rtk_drm_fb_create,
	.output_poll_changed = rtk_drm_output_poll_changed,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

void rtk_drm_mode_config_init(struct drm_device *dev)
{
	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;

	if (rtk_drm_recovery) {
		dev->mode_config.max_width = 1920;
		dev->mode_config.max_height = 1080;
	} else {
		dev->mode_config.max_width = 4096;
		dev->mode_config.max_height = 4096;
	}

	dev->mode_config.cursor_width  = RTK_DRM_FB_MIN_WIDTH;
	dev->mode_config.cursor_height = RTK_DRM_FB_MIN_HEIGHT;

	dev->mode_config.fb_modifiers_not_supported  = false;
	dev->mode_config.funcs = &rtk_drm_mode_config_funcs;
	dev->mode_config.helper_private = &rtk_drm_mode_config_helpers;
}
