#
# Android.mk
#
# Copyright (C) 2014 Zoran Markovic <zoran.markovic@linaro.org>
#
# This file is subject to the terms and conditions of the GNU General Public
# License, version 2. 
#
LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := optional
LOCAL_SHARED_LIBRARIES := libstlport

LOCAL_MODULE := idlestat

LOCAL_C_INCLUDES +=	bionic \

LOCAL_SRC_FILES += \
	idlestat.c \
	topology.c \
	trace.c    \
	utils.c   \

include $(BUILD_EXECUTABLE)
