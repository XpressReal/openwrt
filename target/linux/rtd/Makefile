# SPDX-License-Identifier: GPL-2.0-only

include $(TOPDIR)/rules.mk

ARCH:=arm
BOARD:=rtd
BOARDNAME:=RealTek DHC
FEATURES:=squashfs ramdisk usb ext4
SUBTARGETS:=rtd1619b

KERNEL_PATCHVER:=6.6

KERNELNAME:=Image dtbs

define Target/Description
 Build firmware images for RealTek DHC devices.
endef

include $(INCLUDE_DIR)/target.mk

DEFAULT_PACKAGES += uboot-envtools partx-utils e2fsprogs mkf2fs kmod-gpio-button-hotplug

$(eval $(call BuildTarget))