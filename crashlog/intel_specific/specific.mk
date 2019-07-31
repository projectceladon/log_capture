#
# Copyright (C) Intel 2019
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
SPECIFIC_PATH := intel_specific

#fw crash
ifneq ($(filter broxton, $(TARGET_BOARD_PLATFORM)),)
LOCAL_CFLAGS += -DCONFIG_SSRAM_CRASHLOG -DCONFIG_SSRAM_CRASHLOG_BROXTON
LOCAL_SRC_FILES += $(SPECIFIC_PATH)/intelfwcrash.c
endif

#IBECC
#ifeq ($(TARGET_BOARD_PLATFORM), elkhartlake)
LOCAL_SRC_FILES += $(SPECIFIC_PATH)/intel_ecc_handler.c
LOCAL_CFLAGS += -DCONFIG_ECC
#endif

# defined in mixin/group/debug-lct
# https://wiki.ith.intel.com/display/ANDROIDSI/Generating+a+Crash+Tool+event+from+a+user-space+application
# lct: Generating a Crash Tool event at runtime from a user-space application
# kct: Generating a Crash Tool event at runtime from a kernel module
ifeq ($(CRASHLOGD_MODULE_LCT),true)
LOCAL_CFLAGS += -DCRASHLOGD_MODULE_LCT
LOCAL_SRC_FILES += \
    $(SPECIFIC_PATH)/lct_link.c
LOCAL_SHARED_LIBRARIES += liblctsock
ifneq ($(CRASHLOGD_MODULE_KCT),true)
    LOCAL_SRC_FILES += \
    $(SPECIFIC_PATH)/ct_utils.c \
    $(SPECIFIC_PATH)/ct_eventintegrity.c
endif
endif

ifeq ($(CRASHLOGD_MODULE_KCT),true)
LOCAL_CFLAGS += -DCRASHLOGD_MODULE_KCT
LOCAL_SRC_FILES += \
    $(SPECIFIC_PATH)/ct_utils.c \
    $(SPECIFIC_PATH)/kct_netlink.c \
    $(SPECIFIC_PATH)/ct_eventintegrity.c
endif

#check partition
ifeq ($(CRASHLOGD_PARTITIONS_CHECK),true)
LOCAL_CFLAGS += -DCONFIG_PARTITIONS_CHECK
LOCAL_SRC_FILES += $(SPECIFIC_PATH)/check_partition.c
endif

ifeq ($(CRASHLOGD_FACTORY_CHECKSUM),true)
LOCAL_CFLAGS += -DCONFIG_FACTORY_CHECKSUM
LOCAL_SRC_FILES += $(SPECIFIC_PATH)/checksum.c \
	$(SPECIFIC_PATH)/check_partition.c
endif

#firmware
ifeq ($(CRASHLOGD_MODULE_FW_UPDATE),true)
LOCAL_CFLAGS += -DCRASHLOGD_MODULE_FW_UPDATE
LOCAL_SRC_FILES += $(SPECIFIC_PATH)/fw_update.c
endif

ifeq ($(CRASHLOGD_MODULE_FABRIC),true)
LOCAL_CFLAGS += -DCRASHLOGD_MODULE_FABRIC
LOCAL_SRC_FILES += $(SPECIFIC_PATH)/fabric.c
endif

#boot reason
ifeq ($(CRASHLOGD_ARCH),efilinux)
LOCAL_CFLAGS += -DCONFIG_EFILINUX
LOCAL_SRC_FILES += $(SPECIFIC_PATH)/uefivar.c
LOCAL_STATIC_LIBRARIES += \
    libdmi \
    libuefivar
endif

ifeq ($(CRASHLOGD_ARCH),fdk)
LOCAL_CFLAGS += -DCONFIG_FDK
endif

ifeq ($(CRASHLOGD_ARCH),sofia)
LOCAL_CFLAGS += -DCONFIG_SOFIA
LOCAL_SRC_FILES += $(SPECIFIC_PATH)/vmmtrap.c
LOCAL_SRC_FILES += $(SPECIFIC_PATH)/last_vmm_log.c
endif

ifeq ($(CRASHLOGD_ARCH),sofia_legacy)
LOCAL_CFLAGS += -DCONFIG_SOFIA_LEGACY
LOCAL_SRC_FILES += $(SPECIFIC_PATH)/vmmtrap.c
endif

#Modules
ifeq ($(CRASHLOGD_MODULE_IPTRAK),true)
LOCAL_CFLAGS += -DCRASHLOGD_MODULE_IPTRAK
LOCAL_SRC_FILES += $(SPECIFIC_PATH)/iptrak.c
endif

ifeq ($(CRASHLOGD_MODULE_SPID),true)
LOCAL_CFLAGS += -DCRASHLOGD_MODULE_SPID
LOCAL_SRC_FILES += $(SPECIFIC_PATH)/spid.c
endif

# crashlogd modem configuration file


ifeq ($(CRASHLOGD_MODULE_MODEM),true)
ifeq ($(BOARD_HAVE_MODEM),true)
ifneq ($(CRASHLOGD_NUM_MODEMS),)
LOCAL_CFLAGS += -DCONFIG_NUM_MODEMS=$(CRASHLOGD_NUM_MODEMS)
endif

LOCAL_CFLAGS += -DCRASHLOGD_MODULE_MODEM
LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/libtcs
LOCAL_SHARED_LIBRARIES += \
    libmdmcli \
    libtcs
LOCAL_SRC_FILES += \
    $(SPECIFIC_PATH)/modem.c \
    $(SPECIFIC_PATH)/tcs_wrapper.c \
    $(SPECIFIC_PATH)/mmgr_source.c
endif
endif
LOCAL_C_INCLUDES += $(LOCAL_PATH)/$(SPECIFIC_PATH)
INGREDIENTS_FILES += $(LOCAL_PATH)/$(SPECIFIC_PATH)/ingredients.conf
