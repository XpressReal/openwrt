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

#ifndef _RTK_DRM_PLANE_H_
#define _RTK_DRM_PLANE_H_

int rtk_plane_init(struct drm_device *dev, struct drm_plane *plane,
		   unsigned long possible_crtcs, enum drm_plane_type type);
int rtk_plane_export_refclock_fd_ioctl(struct drm_device *dev,
		   void *data, struct drm_file *file);
int rtk_plane_set_pause_ioctl(struct drm_device *dev,
			    void *data, struct drm_file *file);
int rtk_plane_get_plane_id(struct drm_device *dev,
			    void *data, struct drm_file *file);
int rtk_plane_set_q_param(struct drm_device *dev, void *data, struct drm_file *file);
int rtk_plane_config_channel_lowdelay(struct drm_device *dev, void *data, struct drm_file *file);
int rtk_plane_get_privateinfo(struct drm_device *dev, void *data, struct drm_file *file);
int rtk_plane_query_dispwin_new(struct drm_device *dev, void *data, struct drm_file *file);
int rtk_plane_set_speed(struct drm_device *dev, void *data, struct drm_file *file);
int rtk_plane_set_background(struct drm_device *dev, void *data, struct drm_file *file);
int rtk_plane_keep_curpic(struct drm_device *dev, void *data, struct drm_file *file);
int rtk_plane_keep_curpic_fw(struct drm_device *dev, void *data, struct drm_file *file);
int rtk_plane_keep_curpic_svp(struct drm_device *dev, void *data, struct drm_file *file);
int rtk_plane_set_deintflag(struct drm_device *dev, void *data, struct drm_file *file);
int rtk_plane_create_graphic_win(struct drm_device *dev, void *data, struct drm_file *file);
int rtk_plane_draw_graphic_win(struct drm_device *dev, void *data, struct drm_file *file);
int rtk_plane_modify_graphic_win(struct drm_device *dev, void *data, struct drm_file *file);
int rtk_plane_delete_graphic_win(struct drm_device *dev, void *data, struct drm_file *file);
int rtk_plane_conf_osd_palette(struct drm_device *dev, void *data, struct drm_file *file);
int rtk_plane_conf_plane_mixer(struct drm_device *dev, void *data, struct drm_file *file);
int rtk_plane_set_sdrflag(struct drm_device *dev, void *data, struct drm_file *file);
int rtk_plane_set_flush_ioctl(struct drm_device *dev, void *data, struct drm_file *file);
int rtk_plane_set_tv_system(struct drm_device *dev, void *data, struct drm_file *file);
int rtk_plane_get_tv_system(struct drm_device *dev, void *data, struct drm_file *file);
int rtk_plane_set_dispout_format(struct drm_device *dev, void *data, struct drm_file *file);
int rtk_plane_get_dispout_format(struct drm_device *dev, void *data, struct drm_file *file);
int rtk_plane_set_hdmi_audio_mute(struct drm_device * dev, void *data, struct drm_file *file);
#endif  /* _RTK_DRM_PLANE_H_ */
