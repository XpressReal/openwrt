/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2019 RealTek Inc.
 */

#ifndef _RTK_DRM_DRV_H
#define _RTK_DRM_DRV_H

#include "rtk_drm_rpc.h"

#define RTK_MAX_CRTC		1
#define RTK_MAX_FB_BUFFER	3
#define RTK_GEM_INFO_MAX	128

/* VO specific ioctl */
#define RTK_GET_UNLOCK_BUF   0x0
#define RTK_GET_BUF_ST   0x1
#define RTK_EXPORT_REFCLOCK_FD   0x2
#define RTK_SET_PAUSE   0x3
#define RTK_GET_PLANE_ID   0x4
#define RTK_SET_Q_PAARM 0x5
#define RTK_CONF_CHANNEL_LOWDELAY 0x6
#define RTK_GET_PRIVATEINFO 0x7
#define RTK_QUERY_DISPWIN_NEW 0x8
#define RTK_SET_SPEED 0x9
#define RTK_SET_BACKGROUND 0xa
#define RTK_KEEP_CURPIC 0xb
#define RTK_KEEP_CURPIC_FW 0xc
#define RTK_KEEP_CURPIC_SVP 0xd
#define RTK_SET_DEINTFLAG 0xe
#define RTK_CREATE_GRAPHIC_WIN 0xf
#define RTK_DRAW_GRAPHIC_WIN 0x10
#define RTK_MODIFY_GRAPHIC_WIN 0x11
#define RTK_DELETE_GRAPHIC_WIN 0x12
#define RTK_CONF_OSD_PALETTE 0x13
#define RTK_CONF_PLANE_MIXER 0x14
#define RTK_IOCTL_RESERVED15 0x15 /* RESERVED */
#define RTK_IOCTL_RESERVED16 0x16 /* RESERVED */
#define RTK_SET_SDRFLAG 0x17
#define RTK_SET_FLUSH 0x18
/* HDMI specific ioctl */
#define RTK_SET_TV_SYSTEM 0x20
#define RTK_GET_TV_SYSTEM 0x21
#define RTK_SET_DISPOUT_FORMAT 0x22
#define RTK_GET_DISPOUT_FORMAT 0x23
#define RTK_SET_HDMI_AUDIO_MUTE 0x24

#define DRM_IOCTL_RTK_GET_UNLOCK_BUF     DRM_IOWR( DRM_COMMAND_BASE + RTK_GET_UNLOCK_BUF, struct drm_rtk_buf_st)
#define DRM_IOCTL_RTK_GET_BUF_ST     DRM_IOWR( DRM_COMMAND_BASE + RTK_GET_BUF_ST, struct drm_rtk_buf_st)
#define DRM_IOCTL_RTK_EXPORT_REFCLOCK_FD     DRM_IOWR( DRM_COMMAND_BASE + RTK_EXPORT_REFCLOCK_FD, struct drm_rtk_refclk)
#define DRM_IOCTL_RTK_SET_PAUSE     DRM_IOWR( DRM_COMMAND_BASE + RTK_SET_PAUSE, struct drm_rtk_pause)
#define DRM_IOCTL_RTK_GET_PLANE_ID     DRM_IOWR( DRM_COMMAND_BASE + RTK_GET_PLANE_ID, struct drm_rtk_vo_plane)
#define DRM_IOCTL_RTK_SET_Q_PARAM     DRM_IOWR( DRM_COMMAND_BASE + RTK_SET_Q_PAARM, struct rpc_set_q_param)
#define DRM_IOCTL_RTK_CONF_CHANNEL_LOWDELAY     DRM_IOWR( DRM_COMMAND_BASE + RTK_CONF_CHANNEL_LOWDELAY, struct rpc_config_channel_lowdelay)
#define DRM_IOCTL_RTK_GET_PRIVATEINFO     DRM_IOWR( DRM_COMMAND_BASE + RTK_GET_PRIVATEINFO, struct rpc_privateinfo_param)
#define DRM_IOCTL_RTK_QUERY_DISPWIN_NEW     DRM_IOWR( DRM_COMMAND_BASE + RTK_QUERY_DISPWIN_NEW, struct rpc_query_disp_win_out_new)
#define DRM_IOCTL_RTK_SET_SPEED     DRM_IOWR( DRM_COMMAND_BASE + RTK_SET_SPEED, struct rpc_set_speed)
#define DRM_IOCTL_RTK_SET_BACKGROUND     DRM_IOWR( DRM_COMMAND_BASE + RTK_SET_BACKGROUND, struct rpc_set_background)
#define DRM_IOCTL_RTK_KEEP_CURPIC     DRM_IOWR( DRM_COMMAND_BASE + RTK_KEEP_CURPIC, struct rpc_keep_curpic)
#define DRM_IOCTL_RTK_KEEP_CURPIC_FW     DRM_IOWR( DRM_COMMAND_BASE + RTK_KEEP_CURPIC_FW, struct rpc_keep_curpic)
#define DRM_IOCTL_RTK_KEEP_CURPIC_SVP     DRM_IOWR( DRM_COMMAND_BASE + RTK_KEEP_CURPIC_SVP, struct rpc_keep_curpic_svp)
#define DRM_IOCTL_RTK_SET_DEINTFLAG     DRM_IOWR( DRM_COMMAND_BASE + RTK_SET_DEINTFLAG, struct rpc_set_deintflag)
#define DRM_IOCTL_RTK_CREATE_GRAPHIC_WIN     DRM_IOWR( DRM_COMMAND_BASE + RTK_CREATE_GRAPHIC_WIN, struct rpc_create_graphic_win)
#define DRM_IOCTL_RTK_DRAW_GRAPHIC_WIN     DRM_IOWR( DRM_COMMAND_BASE + RTK_DRAW_GRAPHIC_WIN, struct rpc_draw_graphic_win)
#define DRM_IOCTL_RTK_MODIFY_GRAPHIC_WIN     DRM_IOWR( DRM_COMMAND_BASE + RTK_MODIFY_GRAPHIC_WIN, struct rpc_modify_graphic_win)
#define DRM_IOCTL_RTK_DELETE_GRAPHIC_WIN     DRM_IOWR( DRM_COMMAND_BASE + RTK_DELETE_GRAPHIC_WIN, struct rpc_delete_graphic_win)
#define DRM_IOCTL_RTK_CONF_OSD_PALETTE     DRM_IOWR( DRM_COMMAND_BASE + RTK_CONF_OSD_PALETTE, struct rpc_config_osd_palette)
#define DRM_IOCTL_RTK_CONF_PLANE_MIXER     DRM_IOWR( DRM_COMMAND_BASE + RTK_CONF_PLANE_MIXER, struct rpc_config_plane_mixer)
#define DRM_IOCTL_RTK_SET_SDRFLAG     DRM_IOWR( DRM_COMMAND_BASE + RTK_SET_SDRFLAG, struct rpc_set_sdrflag)
#define DRM_IOCTL_RTK_SET_FLUSH     DRM_IOWR( DRM_COMMAND_BASE + RTK_SET_FLUSH, uint32_t)

#define DRM_IOCTL_RTK_SET_TV_SYSTEM     DRM_IOWR( DRM_COMMAND_BASE + RTK_SET_TV_SYSTEM, struct rpc_config_tv_system)
#define DRM_IOCTL_RTK_GET_TV_SYSTEM     DRM_IOWR( DRM_COMMAND_BASE + RTK_GET_TV_SYSTEM, struct rpc_config_tv_system)
#define DRM_IOCTL_RTK_SET_DISPOUT_FORMAT     DRM_IOWR( DRM_COMMAND_BASE + RTK_SET_DISPOUT_FORMAT, struct rpc_display_output_format)
#define DRM_IOCTL_RTK_GET_DISPOUT_FORMAT     DRM_IOWR( DRM_COMMAND_BASE + RTK_GET_DISPOUT_FORMAT, struct rpc_display_output_format)
#define DRM_IOCTL_RTK_SET_HDMI_AUDIO_MUTE    DRM_IOWR( DRM_COMMAND_BASE + RTK_SET_HDMI_AUDIO_MUTE, struct rpc_audio_mute_info)

struct drm_rtk_buf_st {
	uint32_t plane_id;
	uint32_t idx;
	uint8_t st;
};

struct drm_rtk_refclk {
	uint32_t plane_id;
	int32_t fd;
};

struct drm_rtk_vo_plane {
	uint32_t plane_id;
	uint32_t vo_plane;
};

struct drm_rtk_pause {
	uint32_t plane_id;
	uint8_t enable;
};

struct rtk_gem_object_info {
	const char *name;
	dma_addr_t paddr[RTK_GEM_INFO_MAX];
	u32 num_allocated;
	u32 size_allocated;
};

struct rtk_drm_private {
	struct rtk_gem_object_info *obj_info;
	struct mutex obj_lock;
	struct rtk_rpc_info rpc_info;
	int obj_info_num;
};

extern unsigned int rtk_drm_recovery;
extern struct platform_driver rtk_crtc_platform_driver;
extern struct platform_driver rtk_hdmi_driver;
extern struct platform_driver rtk_dptx_driver;
extern struct platform_driver rtk_cvbs_driver;
extern struct platform_driver rtk_dsi_driver;

#endif /* _RTK_DRM_DRV_H_ */
