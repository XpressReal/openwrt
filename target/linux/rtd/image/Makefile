include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/image.mk

FAT32_BLOCK_SIZE=1024
FAT32_BLOCKS=$(shell echo $$(($(CONFIG_RTD_SD_BOOT_PARTSIZE)*1024*1024/$(FAT32_BLOCK_SIZE))))

# Custom kernel build rule for FIT image
define Build/rtd-fit
    cp $(KDIR)/Image $(STAGING_DIR_HOST)/bin/
    
    # Copy device tree blob
    cp $(KDIR)/image-rtd1619b-bleedingedge-4gb.dtb $(STAGING_DIR_HOST)/bin/ || \
    cp $(LINUX_DIR)/arch/arm64/boot/dts/realtek/rtd1619b-bleedingedge-4gb.dtb $(STAGING_DIR_HOST)/bin/
    
    cp $(CURDIR)/yocto.its $(STAGING_DIR_HOST)/bin/
    
    cd $(STAGING_DIR_HOST)/bin && \
    $(STAGING_DIR_HOST)/bin/mkimage -f yocto.its yocto.itb
    
    cp $(STAGING_DIR_HOST)/bin/yocto.itb $@
endef

define Build/rtd-sdcard
  rm -f $@.boot
  mkfs.fat $@.boot -C $(FAT32_BLOCKS)
  mcopy -i $@.boot ${IMAGE_KERNEL} ::yocto.itb

  ./gen_rtd_sdcard_img.sh $@ \
    $@.boot \
    $(IMAGE_ROOTFS) \
    $(CONFIG_RTD_SD_BOOT_PARTSIZE) \
    $(CONFIG_TARGET_ROOTFS_PARTSIZE)
  rm -f $@.boot
endef

define Device/Default
  PROFILES := Default
  KERNEL_NAME := Image
  KERNEL_SUFFIX := -yocto.itb
  KERNEL := kernel-bin | rtd-fit
  IMAGES := sdcard.img.gz
  IMAGE/sdcard.img.gz := rtd-sdcard | gzip | append-metadata
endef

include $(SUBTARGET).mk

$(eval $(call BuildImage))