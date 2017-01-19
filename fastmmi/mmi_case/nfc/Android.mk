LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE_OWNER := qti
LOCAL_PROPRIETARY_MODULE := true

LOCAL_SRC_FILES := nfc.cpp

LOCAL_MODULE := mmi_nfc
LOCAL_MODULE_TAGS := optional

LOCAL_C_INCLUDES := $(MMI_ROOT)/libmmi \
                    external/connectivity/stlport/stlport \
                    bootable/recovery/minui

LOCAL_CFLAGS := -Wall

LOCAL_SHARED_LIBRARIES := libmmi libcutils libminui libhardware

LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
LOCAL_ADDITIONAL_DEPENDENCIES := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr

include $(BUILD_SHARED_LIBRARY)

