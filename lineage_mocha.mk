# Inherit device configuration for mocha.
$(call inherit-product, device/xiaomi/mocha/full_mocha.mk)

# Inherit some common LineageOS stuff.
$(call inherit-product, vendor/lineage/config/common_mini_tablet_wifionly.mk)

PRODUCT_NAME := lineage_mocha
PRODUCT_DEVICE := mocha
BOARD_VENDOR := Xiaomi

PRODUCT_GMS_CLIENTID_BASE := android-xiaomi

# Keep authenticated ADB available after clean-data installs. The build script
# supplies this host public key; PRODUCT_ADB_KEYS installs it as /adb_keys.
ifneq ($(strip $(MOCHA_ADB_PUBLIC_KEY)),)
PRODUCT_ADB_KEYS := $(MOCHA_ADB_PUBLIC_KEY)
endif
