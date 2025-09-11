# SPDX-License-Identifier: GPL-2.0-or-later

include $(TOPDIR)/rules.mk

PKG_NAME:=aic8800-firmware
PKG_RELEASE:=7

PKG_MAINTAINER:=yibo <yibo@fydeos.io>

include $(INCLUDE_DIR)/package.mk

define Package/aic8800-firmware-default
  SECTION:=firmware
  CATEGORY:=Firmware
endef

define Build/Compile
	true
endef

define Package/aic8800-firmware
  $(Package/aic8800-firmware-default)
  TITLE:=AIC8800 Firmware
endef

define Package/aic8800-firmware/install
	$(INSTALL_DIR) $(1)/lib/firmware/aic8800/
	$(INSTALL_DATA) ./files/* $(1)/lib/firmware/aic8800/
endef

$(eval $(call BuildPackage,aic8800-firmware))