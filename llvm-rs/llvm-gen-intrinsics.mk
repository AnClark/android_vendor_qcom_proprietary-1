# We treat Intrinsics.td as a very special target just like what lib/VMCore/Makefile does
INTRINSICTD := $(LLVM_ROOT_PATH)/include/llvm/Intrinsics.td
INTRINSICTDS := $(wildcard $(dir $(INTRINSICTD))/Intrinsics*.td)

LOCAL_SRC_FILES := $(INTRINSICTD) $(LOCAL_SRC_FILES)

ifeq ($(LOCAL_MODULE_CLASS),)
	LOCAL_MODULE_CLASS := STATIC_LIBRARIES
endif

GENFILE := $(addprefix $(call local-intermediates-dir)/llvm/,Intrinsics.gen)
LOCAL_GENERATED_SOURCES += $(GENFILE)
$(GENFILE): $(INTRINSICTD) $(INTRINSICTDS) $(TBLGEN) $(CLANG_TBLGEN)
ifeq ($(LOCAL_IS_HOST_MODULE),true)
	$(call transform-host-td-to-out,intrinsic)
else
	$(call transform-device-td-to-out,intrinsic)
endif
