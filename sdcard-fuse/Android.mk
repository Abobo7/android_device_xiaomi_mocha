LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE := sdcard-fuse
LOCAL_SRC_FILES := main.c sdcard.cpp fuse.cpp
LOCAL_CFLAGS := -Wall -Wno-unused-parameter -Werror
LOCAL_SHARED_LIBRARIES := libbase libcutils libminijail libpackagelistparser
LOCAL_SANITIZE := integer
LOCAL_MODULE_TAGS := optional
include $(BUILD_EXECUTABLE)
