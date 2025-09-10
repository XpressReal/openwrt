include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/image.mk

FAT32_BLOCK_SIZE=1024
FAT32_BLOCKS=$(shell echo $$(($(CONFIG_RTD_SD_BOOT_PARTSIZE)*1024*1024/$(FAT32_BLOCK_SIZE))))

# Custom kernel build rule for FIT image
define Build/rtd-fit
    # Create unique temporary directory to avoid race conditions
    $(eval TMP_DIR := $(shell mktemp -d))
    
    cp $(KDIR)/Image $(TMP_DIR)/
    
    # Copy device tree blob
    cp $(KDIR)/image-rtd1619b-bleedingedge-4gb.dtb $(TMP_DIR)/ || \
    cp $(LINUX_DIR)/arch/arm64/boot/dts/realtek/rtd1619b-bleedingedge-4gb.dtb $(TMP_DIR)/
    
    cp $(CURDIR)/yocto.its $(TMP_DIR)/
    
    cd $(TMP_DIR) && \
    $(STAGING_DIR_HOST)/bin/mkimage -f yocto.its yocto.itb
    
    cp $(TMP_DIR)/yocto.itb $@
    
    # Clean up temporary directory
    rm -rf $(TMP_DIR)
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