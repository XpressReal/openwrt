# SPDX-License-Identifier: GPL-2.0-or-later

include $(TOPDIR)/rules.mk

PKG_NAME:=realtek-rtd1619b-firmware
PKG_RELEASE:=7

PKG_MAINTAINER:=yibo <yibo@fydeos.io>

include $(INCLUDE_DIR)/package.mk

define Package/realtek-rtd1619b-firmware-default
  SECTION:=firmware
  CATEGORY:=Firmware
endef

define Build/Compile
	true
endef

define Package/realtek-rtd1619b-firmware
  $(Package/realtek-rtd1619b-firmware-default)
  TITLE:=Realtek Firmware
endef

define Package/realtek-rtd1619b-firmware/install
	$(INSTALL_DIR) $(1)/lib/firmware/realtek/rtd1619b
	$(INSTALL_DATA) ./files/rtd1619b/* $(1)/lib/firmware/realtek/rtd1619b/
endef

$(eval $(call BuildPackage,realtek-rtd1619b-firmware))