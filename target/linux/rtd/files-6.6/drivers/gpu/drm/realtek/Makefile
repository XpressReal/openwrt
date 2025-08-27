# SPDX-License-Identifier: GPL-2.0

ccflags-y += -I$(srctree)/drivers/video/fbdev/rtk
ccflags-y += -Werror

rtk_drm-y := rtk_drm_rpc.o \
	     rtk_drm_plane.o \
	     rtk_drm_fence.o \
	     rtk_drm_crtc.o \
	     rtk_drm_gem.o \
	     rtk_drm_fb.o \
	     rtk_drm_drv.o \
	     rtk_hdmi.o \
	     rtk_hdmi_edid.o \
             rtk_cvbs.o \
	     rtk_dptx_core.o \
	     rtk_dptx_reg.o \
	     rtk_dsi.o \
	     rtk_hdcp.o

ifdef CONFIG_REALTEK_TEE
rtk_drm-y += rtk_hdcp1_tee.o \
             rtk_hdcp2_tee.o
endif

obj-$(CONFIG_DRM_RTK) += rtk_drm.o

