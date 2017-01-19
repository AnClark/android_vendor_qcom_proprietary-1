LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_SRC_FILES := pm-proxy.c
LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/libmdmdetect/inc/
LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/libperipheralclient/inc/
LOCAL_SHARED_LIBRARIES := libperipheral_client libcutils libmdmdetect
LOCAL_MODULE := pm-proxy
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_OWNER := qti
include $(BUILD_EXECUTABLE)
