MPDCVS_BOARD_PLATFORM_LIST := msm8660
MPDCVS_BOARD_PLATFORM_LIST += msm8960
MPDCVS_BOARD_PLATFORM_LIST += msm7627a
MPDCVS_BOARD_PLATFORM_LIST += msm8974
MPDCVS_BOARD_PLATFORM_LIST += msm8226
MPDCVS_BOARD_PLATFORM_LIST += msm8610
MPDCVS_BOARD_PLATFORM_LIST += apq8084
MPDCVS_BOARD_PLATFORM_LIST += mpq8092
ifeq ($(TARGET_ARCH),$(filter $(TARGET_ARCH),aarch64 arm64))#Disable for 64k/64u for 8916
  $(info TODOAArch64: $(LOCAL_PATH)/Android.mk: Enable build support for 64 bit)
else
  MPDCVS_BOARD_PLATFORM_LIST += msm8916
endif

ifeq ($(call is-board-platform-in-list,$(MPDCVS_BOARD_PLATFORM_LIST)),true)

IS_MSM7627A:=$(strip $(call is-board-platform-in-list,msm7627a))

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := decision.c
LOCAL_SRC_FILES += ./decision-cores.c

LOCAL_SHARED_LIBRARIES := libcutils
LOCAL_SHARED_LIBRARIES += libutils

LOCAL_MODULE := mpdecision

LOCAL_MODULE_TAGS := optional

LOCAL_CFLAGS += \
	-DUSE_ANDROID_LOG \

ifneq ($(IS_MSM7627A),)
LOCAL_CFLAGS += \
	-DUSE_PM2
endif

USE_MPCTL_SOCKET := true
ifeq ($(USE_MPCTL_SOCKET),true)
    LOCAL_CFLAGS += -DMPCTL_SERVER
    LOCAL_STATIC_LIBRARIES += libqc-mpctl
    LOCAL_SHARED_LIBRARIES += libdl
ifeq ($(TARGET_CPU_SMP),true)
LOCAL_STATIC_LIBRARIES += \
        libqti-perfd-client
endif
endif

LOCAL_MODULE_OWNER := qti
include $(BUILD_EXECUTABLE)

endif
