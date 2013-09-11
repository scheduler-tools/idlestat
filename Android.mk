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
