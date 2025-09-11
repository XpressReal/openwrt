define Device/rtd_xpressreal-t3
  DEVICE_VENDOR := XpressReal
  DEVICE_MODEL := T3
  SOC := rtd1619b
  DEVICE_DTS := realtek/rtd1619b-bleedingedge-4gb
  DEVICE_PACKAGES := aic8800-firmware realtek-rtd1619b-firmware kmod-aic8800-bt kmod-aic8800-wlan
endef
TARGET_DEVICES += rtd_xpressreal-t3