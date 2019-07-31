LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= intel_prop.c

LOCAL_MODULE:= intel_prop
LOCAL_STATIC_LIBRARIES := \
	libcutils

LOCAL_SHARED_LIBRARIES := \
	liblog \
	libc

ifeq ($(INTEL_PROP_LIBDMI),true)
LOCAL_CFLAGS += -DENABLE_DMI
LOCAL_STATIC_LIBRARIES += libdmi
endif
LOCAL_HEADER_LIBRARIES += libutils_headers
LOCAL_PROPRIETARY_MODULE := true
include $(BUILD_EXECUTABLE)
