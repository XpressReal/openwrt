// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2019 Realtek Inc.
 * Author: Simon Hsu <simon_hsu@realtek.com>
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
#include <drm/drm_crtc_helper.h>
#include <drm/drm_debugfs.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>
#include <drm/drm_fbdev_generic.h>
#include <linux/component.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/of_reserved_mem.h>

#include "../drm_internal.h"

#include <linux/dma-map-ops.h>
#include "rtk_drm_drv.h"
#include "rtk_drm_fb.h"
#include "rtk_drm_gem.h"
#include "rtk_drm_rpc.h"
#include "rtk_drm_plane.h"
#include "rtk_drm_fence.h"

#define DRIVER_NAME	"realtek"
#define DRIVER_DESC	"DRM module for RTK"
#define DRIVER_DATE	"20170207"
#define DRIVER_MAJOR	2
#define DRIVER_MINOR	1
#define DRIVER_PATCHLEVEL	1

#define MAX_RTK_SUB_DRIVERS 16

unsigned int rtk_drm_recovery;

module_param_named(recovery, rtk_drm_recovery, int, 0600);

static struct platform_driver *rtk_drm_sub_drivers[MAX_RTK_SUB_DRIVERS];
static int num_sub_drivers;
static struct drm_driver rtk_drm_driver;

#if defined(CONFIG_DEBUG_FS)
static const struct drm_info_list rtk_drm_debugfs_list[] = {
	{"gem_info", rtk_gem_info_debugfs, 0},

};

void rtk_drm_debugfs_init(struct drm_minor *minor)
{
	drm_debugfs_create_files(rtk_drm_debugfs_list,
				ARRAY_SIZE(rtk_drm_debugfs_list),
				minor->debugfs_root, minor);
}

#endif

static int rtk_drm_bind(struct device *dev)
{
	struct drm_device *drm;
	struct rtk_drm_private *priv;
	int ret;

	drm = drm_dev_alloc(&rtk_drm_driver, dev);
	if (IS_ERR(drm))
		return PTR_ERR(drm);

	dev_set_drvdata(dev, drm);

	dev->coherent_dma_mask = DMA_BIT_MASK(32);
	dev->dma_mask = (u64 *)&dev->coherent_dma_mask;

	priv = devm_kzalloc(drm->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		ret = -ENOMEM;
		goto err_free_drm;
	}

	ret = of_reserved_mem_device_init(dev);
	if (ret)
		dev_warn(dev, "init reserved memory failed");

	drm->dev_private = priv;
	mutex_init(&priv->obj_lock);
	ret = rtk_rpc_init(dev, &priv->rpc_info);
	if (ret < 0)
		return ret;
	drm_mode_config_init(drm);

	rtk_drm_mode_config_init(drm);

	ret = component_bind_all(dev, drm);
	if (ret)
		goto err_bind_device;

	drm_kms_helper_poll_init(drm);

	drm_mode_config_reset(drm);

	ret = drm_vblank_init(drm, drm->mode_config.num_crtc);
	if (ret)
		goto err_vblank_init;

	ret = drm_dev_register(drm, 0);
	if (ret)
		goto err_register_drm;

	drm_fbdev_generic_setup(drm, 32);

	return 0;

err_register_drm:
err_vblank_init:
	drm_kms_helper_poll_fini(drm);
	component_unbind_all(dev, drm);
err_bind_device:
err_free_drm:
	drm_dev_put(drm);
	return ret;
}

static void rtk_drm_unbind(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);

	drm_dev_unregister(drm);
	drm_kms_helper_poll_fini(drm);
	component_unbind_all(dev, drm);
	drm_dev_put(drm);
}

static const struct file_operations rtk_drm_driver_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.mmap = rtk_gem_mmap,
	.poll = drm_poll,
	.read = drm_read,
	.unlocked_ioctl = drm_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = drm_compat_ioctl,
#endif
	.release = drm_release,
};

static struct drm_ioctl_desc rtk_drm_ioctls[] = {
	DRM_IOCTL_DEF_DRV(RTK_GET_UNLOCK_BUF, rtk_fence_get_unlock_buf_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RTK_GET_BUF_ST, rtk_fence_get_buf_st_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RTK_EXPORT_REFCLOCK_FD, rtk_plane_export_refclock_fd_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RTK_SET_PAUSE, rtk_plane_set_pause_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RTK_GET_PLANE_ID, rtk_plane_get_plane_id, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RTK_SET_Q_PARAM, rtk_plane_set_q_param, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RTK_CONF_CHANNEL_LOWDELAY, rtk_plane_config_channel_lowdelay, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RTK_GET_PRIVATEINFO, rtk_plane_get_privateinfo, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RTK_QUERY_DISPWIN_NEW, rtk_plane_query_dispwin_new, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RTK_SET_SPEED, rtk_plane_set_speed, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RTK_SET_BACKGROUND, rtk_plane_set_background, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RTK_KEEP_CURPIC, rtk_plane_keep_curpic, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RTK_KEEP_CURPIC_FW, rtk_plane_keep_curpic_fw, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RTK_KEEP_CURPIC_SVP, rtk_plane_keep_curpic_svp, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RTK_SET_DEINTFLAG, rtk_plane_set_deintflag, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RTK_CREATE_GRAPHIC_WIN, rtk_plane_create_graphic_win, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RTK_DRAW_GRAPHIC_WIN, rtk_plane_draw_graphic_win, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RTK_MODIFY_GRAPHIC_WIN, rtk_plane_modify_graphic_win, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RTK_DELETE_GRAPHIC_WIN, rtk_plane_delete_graphic_win, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RTK_CONF_OSD_PALETTE, rtk_plane_conf_osd_palette, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RTK_CONF_PLANE_MIXER, rtk_plane_conf_plane_mixer, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RTK_SET_SDRFLAG, rtk_plane_set_sdrflag, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RTK_SET_FLUSH, rtk_plane_set_flush_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RTK_SET_TV_SYSTEM, rtk_plane_set_tv_system, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RTK_GET_TV_SYSTEM, rtk_plane_get_tv_system, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RTK_SET_DISPOUT_FORMAT, rtk_plane_set_dispout_format, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RTK_GET_DISPOUT_FORMAT, rtk_plane_get_dispout_format, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(RTK_SET_HDMI_AUDIO_MUTE, rtk_plane_set_hdmi_audio_mute, DRM_AUTH|DRM_RENDER_ALLOW),
};

static struct drm_driver rtk_drm_driver = {
	.driver_features	= DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC,
	.dumb_create		= rtk_gem_dumb_create,
	.dumb_map_offset	= rtk_gem_dumb_map_offset,
	.prime_handle_to_fd     = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle     = drm_gem_prime_fd_to_handle,
	.gem_prime_import       = drm_gem_prime_import,
	.gem_prime_import_sg_table = rtk_gem_prime_import_sg_table,
#if defined(CONFIG_DEBUG_FS)
	.debugfs_init		= rtk_drm_debugfs_init,
#endif
	.fops			= &rtk_drm_driver_fops,
	.ioctls         = rtk_drm_ioctls,
	.num_ioctls     = ARRAY_SIZE(rtk_drm_ioctls),
	.name			= DRIVER_NAME,
	.desc			= DRIVER_DESC,
	.date			= DRIVER_DATE,
	.major			= DRIVER_MAJOR,
	.minor			= DRIVER_MINOR,
	.patchlevel		= DRIVER_PATCHLEVEL,
};

static const struct component_master_ops rtk_drm_ops = {
	.bind = rtk_drm_bind,
	.unbind = rtk_drm_unbind,
};

static int compare_dev(struct device *dev, void *data)
{
	struct device_node *np = data;

	if (dev->of_node != np && dev->of_node != np->parent)
		return 0;
	return 1;
}

static int rtk_drm_probe(struct platform_device *pdev)
{
	return drm_of_component_probe(&pdev->dev, compare_dev, &rtk_drm_ops);
}

static int rtk_drm_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &rtk_drm_ops);

	return 0;
}

static void rtk_drm_shutdown(struct platform_device *pdev)
{
	struct drm_device *drm = platform_get_drvdata(pdev);

	if (drm)
		drm_atomic_helper_shutdown(drm);
}

#ifdef CONFIG_PM_SLEEP
static int rtk_drm_suspend(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);

	return drm_mode_config_helper_suspend(drm);
}

static int rtk_drm_resume(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);

	return drm_mode_config_helper_resume(drm);
}

static SIMPLE_DEV_PM_OPS(rtk_drm_pm_ops, rtk_drm_suspend,
			 rtk_drm_resume);
#else
static const struct dev_pm_ops rtk_drm_pm_ops = {};
#endif

static const struct of_device_id rtk_drm_of_ids[] = {
	{ .compatible = "realtek,display-subsystem" },
	{ }
};

static struct platform_driver rtk_drm_platform_driver = {
	.probe	= rtk_drm_probe,
	.remove	= rtk_drm_remove,
	.shutdown = rtk_drm_shutdown,
	.driver	= {
		.name	= "realtek-drm",
		.of_match_table = rtk_drm_of_ids,
		.pm     = &rtk_drm_pm_ops,
	},
};

#define ADD_RTK_SUB_DRIVER(drv, CONFIG_cond) { \
	if (IS_ENABLED(CONFIG_cond) && \
	    !WARN_ON(num_sub_drivers >= MAX_RTK_SUB_DRIVERS)) \
		rtk_drm_sub_drivers[num_sub_drivers++] = &drv; \
}

static int __init rtk_drm_init(void)
{
	int ret;

	num_sub_drivers = 0;
	ADD_RTK_SUB_DRIVER(rtk_crtc_platform_driver, CONFIG_DRM_RTK);
	ADD_RTK_SUB_DRIVER(rtk_hdmi_driver, CONFIG_DRM_RTK);
	ADD_RTK_SUB_DRIVER(rtk_dptx_driver, CONFIG_DRM_RTK);
	ADD_RTK_SUB_DRIVER(rtk_cvbs_driver, CONFIG_DRM_RTK);
	ADD_RTK_SUB_DRIVER(rtk_dsi_driver, CONFIG_DRM_RTK);

	ret = platform_register_drivers(rtk_drm_sub_drivers, num_sub_drivers);
	if (ret)
		return ret;

	ret = platform_driver_register(&rtk_drm_platform_driver);
	if (ret)
		goto err_register_drm;

	return 0;

err_register_drm:
	platform_unregister_drivers(rtk_drm_sub_drivers, num_sub_drivers);
	return ret;
}

static void __exit rtk_drm_fini(void)
{
	platform_driver_unregister(&rtk_drm_platform_driver);

	platform_unregister_drivers(rtk_drm_sub_drivers, num_sub_drivers);
}

late_initcall(rtk_drm_init);
module_exit(rtk_drm_fini);

MODULE_AUTHOR("Simon Hsu <simon_hsu@realtek.com>");
MODULE_DESCRIPTION("REALTEK DRM Driver");
MODULE_IMPORT_NS(DMA_BUF);
MODULE_LICENSE("GPL v2");
