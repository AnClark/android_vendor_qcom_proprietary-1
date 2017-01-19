ifeq ($(call is-board-platform-in-list,msm8226 msm8610 msm8916 msm8916_64),true)
# Disable temporarily for compilling error
MMI_ROOT := $(call my-dir)
include $(call all-subdir-makefiles)
endif
