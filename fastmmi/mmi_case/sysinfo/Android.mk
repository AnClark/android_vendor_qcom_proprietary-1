LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE_OWNER := qcom
LOCAL_PROPRIETARY_MODULE := true
LOCAL_SRC_FILES := sysinfo.cpp

LOCAL_MODULE := mmi_sysinfo
LOCAL_MODULE_TAGS := optional
LOCAL_CFLAGS := -Wall
LOCAL_C_INCLUDES := $(MMI_ROOT)/libmmi
LOCAL_C_INCLUDES += external/connectivity/stlport/stlport
LOCAL_C_INCLUDES += bootable/recovery/minui \
                    vendor/qcom/proprietary/diag/include \
                    vendor/qcom/proprietary/diag/src/ \
                    $(TARGET_OUT_HEADERS)/common/inc \

LOCAL_SHARED_LIBRARIES := libmmi libcutils libdiag

LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
LOCAL_ADDITIONAL_DEPENDENCIES := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr

include $(BUILD_SHARED_LIBRARY)

