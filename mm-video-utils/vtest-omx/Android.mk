LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

ifeq ($(call is-board-platform-in-list,msm8974 msm8226 apq8084 msm8916),true)

libmm-vtest-def := \
	-g -O3 -Werror\
	-D_POSIX_SOURCE -DPOSIX_C_SOURCE=199506L \
	-D_XOPEN_SOURCE=600 \
	-D_ANDROID_ \
	-Wno-deprecated-declarations \

mm-vtest-omx-test-inc		:= $(TARGET_OUT_HEADERS)/mm-core/omxcore
mm-vtest-omx-test-inc		+= $(LOCAL_PATH)/common/inc
mm-vtest-omx-test-inc		+= $(LOCAL_PATH)/utils/inc
mm-vtest-omx-test-inc		+= $(TOP)/vendor/qcom/proprietary/common/inc
mm-vtest-omx-test-inc		+= $(TOP)/frameworks/native/include/media/hardware
mm-vtest-omx-test-inc		+= $(TOP)/hardware/qcom/media/mm-video-v4l2/vidc
mm-vtest-omx-test-inc		+= $(TOP)/hardware/qcom/media/mm-core/inc
mm-vtest-omx-test-inc		+= $(TOP)/hardware/qcom/media/libstagefrighthw/
mm-vtest-omx-test-inc		+= $(TOP)/frameworks/native/include/gui/
mm-vtest-omx-test-inc		+= $(TOP)/frameworks/native/include/ui/
mm-vtest-omx-test-inc		+= $(TOP)/frameworks/native/include/
mm-vtest-omx-test-inc		+= $(TOP)/external/connectivity/stlport/stlport/

ifeq ($(shell if test -f $(TOP)/vendor/qcom/proprietary/securemsm-noship/sampleclient/content_protection_copy.h; then echo true; else echo false; fi;),true)
$(warning SECURE_COPY_ENABLED)
libmm-vtest-def 			+= -DSECURE_COPY_ENABLED
mm-vtest-omx-test-inc		+= $(TOP)/vendor/qcom/proprietary/securemsm-noship/sampleclient/
mm-vtest-omx-test-inc		+= $(TOP)/vendor/qcom/proprietary/securemsm/QSEEComAPI/
endif

LOCAL_MODULE			:= mm-vidc-omx-test
LOCAL_MODULE_TAGS       := optional
LOCAL_CFLAGS            := $(libmm-vtest-def)
LOCAL_C_INCLUDES        := $(mm-vtest-omx-test-inc)
LOCAL_C_INCLUDES        += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
LOCAL_ADDITIONAL_DEPENDENCIES := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr
LOCAL_PRELINK_MODULE    := false

LOCAL_SHARED_LIBRARIES  := libOmxCore libOmxVenc libbinder libcutils
LOCAL_SHARED_LIBRARIES  += libdl libgui libui libutils

LOCAL_SRC_FILES			:= \
				utils/src/vt_queue.c \
				utils/src/vt_semaphore.c \
				utils/src/vt_signal.c \
				utils/src/vt_file.c \
				common/src/vtest_Config.cpp \
				common/src/vtest_File.cpp \
				common/src/vtest_Mutex.cpp \
				common/src/vtest_Parser.cpp \
				common/src/vtest_Queue.cpp \
				common/src/vtest_Script.cpp \
				common/src/vtest_Signal.cpp \
				common/src/vtest_SignalQueue.cpp \
				common/src/vtest_Sleeper.cpp \
				common/src/vtest_Thread.cpp \
				common/src/vtest_Time.cpp \
				common/src/vtest_Crypto.cpp \
				common/src/vtest_BufferManager.cpp \
				common/src/vtest_ISource.cpp \
				common/src/vtest_ITestCase.cpp \
				common/src/vtest_TestCaseFactory.cpp \
				common/src/vtest_Decoder.cpp \
				common/src/vtest_DecoderFileSource.cpp \
				common/src/vtest_DecoderFileSink.cpp \
				common/src/vtest_Encoder.cpp \
				common/src/vtest_EncoderFileSink.cpp \
				common/src/vtest_EncoderFileSource.cpp \
				common/src/vtest_NativeWindow.cpp \
				common/src/vtest_MdpSource.cpp \
				common/src/vtest_MdpOverlaySink.cpp \
				common/src/vtest_TestDecode.cpp \
				common/src/vtest_TestEncode.cpp \
				common/src/vtest_TestTranscode.cpp \
				app/src/vtest_App.cpp

include $(BUILD_EXECUTABLE)

endif
