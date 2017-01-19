# sources and intermediate files are separated

LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)
include $(LOCAL_PATH)/Android.min
LOCAL_ADDITIONAL_DEPENDENCIES += $(LOCAL_PATH)/Android.min

LOCAL_ADDITIONAL_DEPENDENCIES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr
LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include

LOCAL_SRC_FILES := \
	netmgr_client.c \
	netmgr_netlink.c \
	netmgr_util.c

LOCAL_MODULE := libnetmgr
LOCAL_MODULE_TAGS := optional

LOCAL_MODULE_OWNER := qti
LOCAL_PROPRIETARY_MODULE := true

include $(BUILD_SHARED_LIBRARY)

################################################################################

include $(CLEAR_VARS)
include $(LOCAL_PATH)/Android.min
LOCAL_ADDITIONAL_DEPENDENCIES += $(LOCAL_PATH)/Android.min
LOCAL_ADDITIONAL_DEPENDENCIES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr

#LOCAL_C_INCLUDES += external/lql/src
#LOCAL_C_INCLUDES += external/iptables/include
#LOCAL_C_INCLUDES += $(call include-path-for, glib)
#LOCAL_C_INCLUDES += $(call include-path-for, glib)/glib
#LOCAL_C_INCLUDES += $(call include-path-for, glib)/gobject
LOCAL_C_INCLUDES  += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/cne/inc
LOCAL_C_INCLUDES += external/connectivity/stlport/stlport

LOCAL_SRC_FILES := \
	netmgr.c \
	netmgr_exec.c \
	netmgr_kif.c \
	netmgr_main.c \
	netmgr_platform.c \
	netmgr_qmi.c \
	netmgr_sm.c \
	netmgr_sm_int.c \
	netmgr_tc.c \
	netmgr_test.c \
	netmgr_qmi_dfs.c \
	netmgr_iwlan_client.cpp

LOCAL_MODULE := netmgrd
LOCAL_MODULE_TAGS := optional

LOCAL_SHARED_LIBRARIES +=  \
        libqmi             \
        libnetutils        \
        libnetmgr          \
        libqmiservices     \
        libqmi_cci         \
        libqmi_common_so   \
        libqmi_client_qmux

ifeq ($(BOARD_USES_QCNE),true)
LOCAL_SHARED_LIBRARIES += libcneapiclient
LOCAL_SHARED_LIBRARIES += libcnefeatureconfig
LOCAL_SHARED_LIBRARIES += libbinder
endif

LOCAL_MODULE_OWNER := qti

include $(BUILD_EXECUTABLE)
