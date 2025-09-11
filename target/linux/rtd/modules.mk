# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (C) 2013-2016 OpenWrt.org

define KernelPackage/fw-remoteproc-rtk
    SUBMENU:=$(OTHER_MENU)
    TITLE:=Realtek remoteproc firmware loader
    KCONFIG:= \
      CONFIG_REMOTEPROC=y \
      CONFIG_REMOTEPROC_CDEV=y \
      CONFIG_RTK_FW_REMOTEPROC=m
    FILES:= \
      $(LINUX_DIR)/drivers/remoteproc/rtk_fw_remoteproc.ko
    AUTOLOAD:=$(call AutoLoad,50,rtk_fw_remoteproc)
endef

define KernelPackage/fw-remoteproc-rtk/description
  Realtek remoteproc firmware loader
endef

$(eval $(call KernelPackage,fw-remoteproc-rtk))

define KernelPackage/rpmsg-rtk
    SUBMENU:=$(OTHER_MENU)
    TITLE:=Realtek rpmsg bus driver
    DEPENDS:=+kmod-fw-remoteproc-rtk
    KCONFIG:= \
      CONFIG_RPMSG=y \
      CONFIG_RPMSG_CHAR=y \
      CONFIG_RPMSG_CTRL=y \
      CONFIG_RPMSG_NS=y \
      CONFIG_RPMSG_VIRTIO=y \
      CONFIG_RPMSG_RTK_RPC=m
    FILES:= \
      $(LINUX_DIR)/drivers/rpmsg/rpmsg_rtk.ko
    AUTOLOAD:=$(call AutoLoad,51,rpmsg_rtk)
endef

define KernelPackage/rpmsg-rtk/description
  Realtek rpmsg bus driver
endef

$(eval $(call KernelPackage,rpmsg-rtk))

define KernelPackage/krpc-rtk
    SUBMENU:=$(OTHER_MENU)
    TITLE:=Realtek RPC memory driver
    DEPENDS:=+kmod-rpmsg-rtk
    KCONFIG:= \
      CONFIG_RTK_KRPC=m
    FILES:= \
      $(LINUX_DIR)/drivers/soc/realtek/common/rtk_rpc_mem.ko \
      $(LINUX_DIR)/drivers/soc/realtek/common/rtk_krpc_agent.ko \
      $(LINUX_DIR)/drivers/soc/realtek/common/rtk_urpc_service.ko
    AUTOLOAD:=$(call AutoLoad,52,rtk_rpc_mem rtk_krpc_agent rtk_urpc_service)
endef

define KernelPackage/krpc-rtk/description
  Realtek RPC memory driver
endef

$(eval $(call KernelPackage,krpc-rtk))

define KernelPackage/drm-rtk
    SUBMENU:=$(OTHER_MENU)
    TITLE:=Realtek DRM support
    DEPENDS:=+kmod-krpc-rtk
    KCONFIG:= \
      CONFIG_DRM=y \
      CONFIG_FRAMEBUFFER_CONSOLE=y \
      CONFIG_DRM_FBDEV_EMULATION=y \
      CONFIG_RTK_METADATA_AUTOJUDGE=y \
      CONFIG_DRM_RTK_VOWB=y \
      CONFIG_DRM_RTK=m
    FILES:= \
      $(LINUX_DIR)/drivers/gpu/drm/display/drm_display_helper.ko \
      $(LINUX_DIR)/drivers/gpu/drm/drm_dma_helper.ko \
      $(LINUX_DIR)/drivers/gpu/drm/realtek/rtk_drm.ko
    AUTOLOAD:=$(call AutoLoad,53,drm_display_helper drm_dma_helper rtk_drm)
endef

define KernelPackage/drm-rtk/description
  Realtek DRM support
endef

$(eval $(call KernelPackage,drm-rtk))