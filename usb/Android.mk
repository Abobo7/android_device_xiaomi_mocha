LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := android.hardware.usb@1.0-service.mocha
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_PROPRIETARY_MODULE := true
LOCAL_SRC_FILES := service.cpp
LOCAL_CFLAGS := -Wall -Wno-unused-parameter -Werror
LOCAL_SHARED_LIBRARIES := \
    libcutils \
    libhidlbase \
    libhidltransport \
    liblog \
    libutils \
    android.hardware.usb@1.0
LOCAL_INIT_RC := android.hardware.usb@1.0-service.mocha.rc
include $(BUILD_EXECUTABLE)
