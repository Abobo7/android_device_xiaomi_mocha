LOCAL_PATH:= $(call my-dir)

# Android 10 first-stage init needs this before system-as-root is mounted.
# Keep the normal root copy for second-stage init, recovery and vold.
include $(CLEAR_VARS)
LOCAL_MODULE       := fstab.tn8.first_stage
LOCAL_MODULE_STEM  := fstab.tn8
LOCAL_MODULE_CLASS := ETC
LOCAL_SRC_FILES    := etc/fstab.tn8
LOCAL_MODULE_PATH  := $(TARGET_RAMDISK_OUT)
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE       := fstab.tn8
LOCAL_MODULE_CLASS := ETC
LOCAL_SRC_FILES    := etc/fstab.tn8
LOCAL_MODULE_PATH  := $(TARGET_ROOT_OUT)
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE       := init.cal.rc
LOCAL_MODULE_CLASS := ETC
LOCAL_SRC_FILES    := etc/init.cal.rc
LOCAL_MODULE_PATH  := $(TARGET_ROOT_OUT)
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE       := init.comms.rc
LOCAL_MODULE_CLASS := ETC
LOCAL_SRC_FILES    := etc/init.comms.rc
LOCAL_MODULE_PATH  := $(TARGET_ROOT_OUT)
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE       := init.hdcp.rc
LOCAL_MODULE_CLASS := ETC
LOCAL_SRC_FILES    := etc/init.hdcp.rc
LOCAL_MODULE_PATH  := $(TARGET_ROOT_OUT)
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE       := init.t124.rc
LOCAL_MODULE_CLASS := ETC
LOCAL_SRC_FILES    := etc/init.t124.rc
LOCAL_MODULE_PATH  := $(TARGET_ROOT_OUT)
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE       := init.tegra.rc
LOCAL_MODULE_CLASS := ETC
LOCAL_SRC_FILES    := etc/init.tegra.rc
LOCAL_MODULE_PATH  := $(TARGET_ROOT_OUT)
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE       := init.tlk.rc
LOCAL_MODULE_CLASS := ETC
LOCAL_SRC_FILES    := etc/init.tlk.rc
LOCAL_MODULE_PATH  := $(TARGET_ROOT_OUT)
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE       := init.tn8.rc
LOCAL_MODULE_CLASS := ETC
LOCAL_SRC_FILES    := etc/init.tn8.rc
LOCAL_MODULE_PATH  := $(TARGET_ROOT_OUT)
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE       := init.tn8.usb.rc
LOCAL_MODULE_CLASS := ETC
LOCAL_SRC_FILES    := etc/init.tn8.usb.rc
LOCAL_MODULE_PATH  := $(TARGET_ROOT_OUT)
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE       := init.tn8_common.rc
LOCAL_MODULE_CLASS := ETC
LOCAL_SRC_FILES    := etc/init.tn8_common.rc
LOCAL_MODULE_PATH  := $(TARGET_ROOT_OUT)
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE       := init.ussrd.rc
LOCAL_MODULE_CLASS := ETC
LOCAL_SRC_FILES    := etc/init.ussrd.rc
LOCAL_MODULE_PATH  := $(TARGET_ROOT_OUT)
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE       := power.tn8.rc
LOCAL_MODULE_CLASS := ETC
LOCAL_SRC_FILES    := etc/power.tn8.rc
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE       := ueventd.tn8.rc
LOCAL_MODULE_CLASS := ETC
LOCAL_SRC_FILES    := etc/ueventd.tn8.rc
LOCAL_MODULE_PATH  := $(TARGET_ROOT_OUT)
include $(BUILD_PREBUILT)

# Stock Tegra unified-scaling initialization, required before ussrd starts.
include $(CLEAR_VARS)
LOCAL_MODULE       := ussr_setup.sh
LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_MODULE_TAGS  := optional
LOCAL_SRC_FILES    := bin/ussr_setup.sh
LOCAL_MODULE_PATH  := $(TARGET_OUT_EXECUTABLES)
LOCAL_STRIP_MODULE := false
include $(BUILD_PREBUILT)
