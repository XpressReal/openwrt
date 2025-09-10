# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (C) 2013-2016 OpenWrt.org

define KernelPackage/drm-rtk
    SUBMENU:=$(OTHER_MENU)
    TITLE:=Realtek DRM support
    KCONFIG:= \
      CONFIG_DRM=y \
      CONFIG_DRM_RTK=m \
      CONFIG_FRAMEBUFFER_CONSOLE=y \
      CONFIG_DRM_FBDEV_EMULATION=y \
      CONFIG_RTK_METADATA_AUTOJUDGE=y \
      CONFIG_DRM_RTK_VOWB=y
    FILES:= \
      $(LINUX_DIR)/drivers/remoteproc/rtk_fw_remoteproc.ko \
      $(LINUX_DIR)/drivers/rpmsg/rpmsg_rtk.ko \
      $(LINUX_DIR)/drivers/soc/realtek/common/rtk_rpc_mem.ko \
      $(LINUX_DIR)/drivers/soc/realtek/common/rtk_krpc_agent.ko \
      $(LINUX_DIR)/drivers/soc/realtek/common/rtk_urpc_service.ko \
      $(LINUX_DIR)/drivers/gpu/drm/display/drm_display_helper.ko \
      $(LINUX_DIR)/drivers/gpu/drm/drm_dma_helper.ko \
      $(LINUX_DIR)/drivers/gpu/drm/realtek/rtk_drm.ko

    AUTOLOAD:=$(call AutoLoad,50,drm-rtk)
endef

define KernelPackage/drm-rtk/description
  Realtek DRM support
endef

$(eval $(call KernelPackage,drm-rtk))