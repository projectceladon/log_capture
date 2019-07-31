#
# Copyright (C) Intel 2014
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

LOCAL_PATH := $(call my-dir)

include $(LOCAL_PATH)/test_config.mk

include $(CLEAR_VARS)

LOCAL_MODULE := crashlogd
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_OWNER := intel
LOCAL_HEADER_LIBRARIES += libutils_headers
LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_SRC_FILES := \
    main.c \
    config.c \
    inotify_handler.c \
    startupreason.c \
    crashutils.c \
    usercrash.c \
    anruiwdt.c \
    recovery.c \
    history.c \
    trigger.c \
    dropbox.c \
    fsutils.c \
    panic.c \
    config_handler.c \
    watchdog.c \
    fwcrash.c \
    utils.c \
    getbulkprops.c \
    ingredients.c

LOCAL_SHARED_LIBRARIES := libcutils libcrypto

LOCAL_LDLIBS := -lm -llog
LOCAL_CFLAGS += -D__LINUX__
LOCAL_C_INCLUDES := $(LOCAL_PATH) \
    $(LOCAL_PATH)/inc

include $(LOCAL_PATH)/intel_specific/specific.mk

# Options
CRASHLOGD_LOGS_PATH := "/data/logs"

ifeq ($(MIXIN_DEBUG_LOGS),true)
LOCAL_CFLAGS += -DFULL_REPORT -DCONFIG_APLOG -DCONFIG_USE_SD=FALSE \
    -DCONFIG_LOGS_PATH='$(CRASHLOGD_LOGS_PATH)' \
    -DCONFIG_DUMP_BINDER

LOCAL_CFLAGS += -DCONFIG_RAMDUMP -DCONFIG_RAMDUMP_CRASHLOG
LOCAL_SRC_FILES += ramdump.c
endif

ifeq ($(CRASHLOGD_COREDUMP),true)
LOCAL_CFLAGS += -DCONFIG_COREDUMP
endif

# backtrace
ifeq ($(CRASHLOGD_MODULE_BTDUMP),true)
LOCAL_CFLAGS += -DCONFIG_BTDUMP
LOCAL_STATIC_LIBRARIES += libbtdump
LOCAL_SHARED_LIBRARIES += libbacktrace
#include external/stlport/libstlport.mk
endif

ifeq ($(CRASHLOGD_MODULE_EARLYLOGS),true)
LOCAL_CFLAGS += -DCONFIG_EARLY_LOGS
endif

LOCAL_PROPRIETARY_MODULE := true
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_COPY_HEADERS_TO := crashlog
include $(BUILD_COPY_HEADERS)

# $1 is the *.sh file to copy

define script_to_copy
include $(CLEAR_VARS)
LOCAL_MODULE := $(1)
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_OWNER := intel
LOCAL_SRC_FILES := $(1)
LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_PROPRIETARY_MODULE := true
include $(BUILD_PREBUILT)

endef

script_list := dumpstate_dropbox.sh monitor_crashenv del_hist.sh del_log.sh

$(foreach script,$(script_list),$(eval $(call script_to_copy,$(script))))

include $(CLEAR_VARS)
LOCAL_MODULE := ingredients.conf
LOCAL_MODULE_OWNER := intel
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := ETC
LOCAL_PROPRIETARY_MODULE := true
include $(BUILD_SYSTEM)/base_rules.mk
INGREDIENTS_FILES ?=
INGREDIENTS_FILES := $(LOCAL_PATH)/ingredients.conf $(INGREDIENTS_FILES)

$(LOCAL_BUILT_MODULE) : $(INGREDIENTS_FILES)
	@echo "Building ingredients.conf"
	$(hide) rm -fr $@
	$(hide) for src in $^; do \
		cat $${src} >> $@; \
	done
