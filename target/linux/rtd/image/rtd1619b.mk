define Device/rtd_xpressreal-t3
  DEVICE_VENDOR := XpressReal
  DEVICE_MODEL := T3
  SOC := rtd1619b
  DEVICE_DTS := realtek/rtd1619b-bleedingedge-4gb
  DEVICE_PACKAGES := kmod-aic8800-bt kmod-aic8800-wlan luci
endef
TARGET_DEVICES += rtd_xpressreal-t3