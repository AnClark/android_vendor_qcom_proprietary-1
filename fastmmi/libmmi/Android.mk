LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE_OWNER := qcom
LOCAL_PROPRIETARY_MODULE := true
LOCAL_MODULE:= libmmi
LOCAL_SRC_FILES := mmi_window.cpp \
                   mmi_module_manage.cpp \
                   mmi_button.cpp \
                   mmi_text.cpp \
                   mmi_item.cpp \
                   mmi_key.cpp \
                   mmi_config.cpp \
                   mmi_state.cpp \
                   mmi_utils.cpp \
                   diag_nv.cpp \
                   mmi_lang.cpp \
                   mmi_graphics.cpp

LOCAL_C_INCLUDES := bootable/recovery/minui \
                    external/connectivity/stlport/stlport \
                    vendor/qcom/proprietary/diag/include \
                    vendor/qcom/proprietary/diag/src/ \
                    $(TARGET_OUT_HEADERS)/common/inc \
                    external/libxml2/include \
                    external/icu/icu4c/source/common \
                    external/freetype/include

LOCAL_MODULE_TAGS := optional
LOCAL_CFLAGS := -Wall
LOCAL_SHARED_LIBRARIES := libpixelflinger libcutils libminui libdl libdiag libxml2 libicuuc libft2 libutils

LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
LOCAL_ADDITIONAL_DEPENDENCIES := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr

include $(BUILD_SHARED_LIBRARY)

