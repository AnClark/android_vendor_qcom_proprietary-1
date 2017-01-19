LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	mp-ctl.c

LOCAL_SHARED_LIBRARIES := \
	libcutils

LOCAL_C_INCLUDES += \
	device/qcom/common/power \
	vendor/qcom/proprietary/android-perf/mp-ctl

LOCAL_CFLAGS += \
	-DSERVER \
	-g0 \
	-Wall \
	#-DQC_DEBUG=1

ifeq ($(call is-android-codename,JELLY_BEAN),true)
    LOCAL_CFLAGS += -DANDROID_JELLYBEAN=1

    LOCAL_C_INCLUDES += \
    $(TARGET_OUT_HEADERS)/common/inc
endif

LOCAL_MODULE := libqc-mpctl
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_OWNER := qti

include $(BUILD_STATIC_LIBRARY)
