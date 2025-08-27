include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/image.mk

FAT32_BLOCK_SIZE=1024
FAT32_BLOCKS=$(shell echo $$(($(CONFIG_RTD_SD_BOOT_PARTSIZE)*1024*1024/$(FAT32_BLOCK_SIZE))))

define Build/rtd-sdcard
endef

define Device/Default
  PROFILES := Default
  KERNEL_NAME := Image
  KERNEL := kernel-bin | Image none
  IMAGES := sdcard.img.gz
  IMAGE/sdcard.img.gz := rtd-sdcard | append-metadata | gzip
  DTS_DIR := rtd1619b/
  DTS = $$(DTS_DIR)$$(SOC)-$(lastword $(subst _, ,$(1)))
endef

include $(SUBTARGET).mk

$(eval $(call BuildImage))