/* Copyright (C) Intel 2015
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/types.h>
#include <linux/pci.h>
#include <linux/pci_regs.h>

#include "crashutils.h"
#include "fsutils.h"
#include "privconfig.h"

#if CONFIG_SSRAM_CRASHLOG_BROXTON

#define SSRAM_BAR_MASK				0xFFFFFFF0

#define CRASHLOG_LENGTH                         5120
#define PMC_CRASHLOG_VERSION(base)		(base + 0x680)
#define PMC_RESET_RECORD_REASON(base)		(base + 0x708)
#define PMC_RESET_RECORD_GLOBAL_RST_CAUSE(base)	(base + 0x718)
#define PMC_RESET_RECORD_LAST_EVENT_CAUSE(base)	(base + 0x724)
#define PUNIT_CRASHLOG_VERSION(base)		(base +   0x4)
#define PUNIT_ERROR_RECORD_REASON(base)		(base + 0x28C)
#define CPU_CRASHLOG_VERSION(base)		(base + 0x900)
#define CPU_GLB_CORE0_VALID(base)		(base + 0x934)

#define TEST_BIT(val, bit)			(!!(val & (1UL << bit)))
#define PMC_REASON_GLOBAL_RST_BIT		1
#define PMC_GLOBAL_RST_CAUSE_PCHPWROK_FALL_BIT  19
#define PMC_LAST_EVENT_CAUSE_TCO_BIT		14

static uint32_t get_ssram_base_addr() {
	char buf[64] = { '\0', };
	int ret = 0;
	static uint32_t bar_addr = 0;

	if (bar_addr != 0x0)
		return bar_addr;

	/*The base address is exposed in kernel IPC driver, as attribute*/
	ret = file_read_string(SSRAM_ATTR_FILE_NAME, buf);
	if (ret <= 0)
		return 0;
	sscanf(buf, "%x", &bar_addr);
	bar_addr &= SSRAM_BAR_MASK;
	if ((bar_addr == 0x0) || (bar_addr == SSRAM_BAR_MASK)) {
		LOGE("%s: incorrect ssram bar value 0x%X\n", __func__, bar_addr);
		return 0;
	}

	return bar_addr;
}

int check_fw_crashlog() {
    int i;
    uint32_t crashlog_addr;
    uint32_t pmc_crashlog_version, pmc_crashlog_reason, pmc_global_cause, pmc_event_cause,
        punit_crashlog_version, punit_crashlog_reason, cpu_crashlog_version;
    uint32_t cpu_glb_core_valid[4];

    if (!file_exists(DEV_MEM_FILE)) {
        LOGE("%s: %s not available, abort\n", __func__, DEV_MEM_FILE);
        return 0;
    }

    crashlog_addr = get_ssram_base_addr();
    if (crashlog_addr == 0) {
        LOGE("%s: ssram not available, abort\n", __func__);
        return 0;
    }

    LOGI("%s: checking crashlog region at address: 0x%x, length: %d\n",
         __func__, crashlog_addr, CRASHLOG_LENGTH);

    if (read_dev_mem_region(PMC_CRASHLOG_VERSION(crashlog_addr), 4, &pmc_crashlog_version) ||
        read_dev_mem_region(PMC_RESET_RECORD_REASON(crashlog_addr), 4, &pmc_crashlog_reason) ||
        read_dev_mem_region(PMC_RESET_RECORD_GLOBAL_RST_CAUSE(crashlog_addr), 4, &pmc_global_cause) ||
        read_dev_mem_region(PMC_RESET_RECORD_LAST_EVENT_CAUSE(crashlog_addr), 4, &pmc_event_cause) ||
        read_dev_mem_region(PUNIT_CRASHLOG_VERSION(crashlog_addr), 4, &punit_crashlog_version) ||
        read_dev_mem_region(PUNIT_ERROR_RECORD_REASON(crashlog_addr), 4, &punit_crashlog_reason) ||
        read_dev_mem_region(CPU_CRASHLOG_VERSION(crashlog_addr), 4, &cpu_crashlog_version)) {
        LOGE("%s: reading of crashlog fields failed : %s (%d)\n",
             __func__, strerror(errno), errno);
        return 0;
    }

    for (i = 0; i < 4; i++) {
        if (read_dev_mem_region(CPU_GLB_CORE0_VALID(crashlog_addr) + 4 * i, 4, &(cpu_glb_core_valid[i]))) {
            LOGE("%s: reading of CPU core %d record valid failed : %s (%d)\n",
                 __func__, i, strerror(errno), errno);
            return 0;
        }
    }

    if ((pmc_crashlog_version != 0) &&
        ((TEST_BIT(pmc_crashlog_reason, PMC_REASON_GLOBAL_RST_BIT) &&
          !TEST_BIT(pmc_global_cause, PMC_GLOBAL_RST_CAUSE_PCHPWROK_FALL_BIT)) ||
         TEST_BIT(pmc_event_cause, PMC_LAST_EVENT_CAUSE_TCO_BIT))) {
        LOGI("%s: PMC error detected in crashlog\n", __func__);
        return 1;
    }

    if ((punit_crashlog_version != 0) && (punit_crashlog_reason != 0)) {
        LOGI("%s: PUNIT error detected in crashlog\n", __func__);
        return 1;
    }

    if (cpu_crashlog_version != 0) {
        for (i = 0; i < 4; i++) {
            if (cpu_glb_core_valid[i] != 0) {
                LOGI("%s: CPU error detected in crashlog\n", __func__);
                return 1;
            }
        }
    }

    return 0;
}

#else
#error Unsupported platform
#endif /* CONFIG_SSRAM_CRASHLOG_BROXTON */

int do_fw_crashlog_copy(const char* dir) {
    int ret = -1, fd;
    uint32_t crashlog_addr;
    void *crashlog_region;
    char dstfile[PATHMAX];

    if (!file_exists(DEV_MEM_FILE)) {
        LOGE("%s: %s not available, abort\n", __func__, DEV_MEM_FILE);
        return ret;
    }

    crashlog_addr = get_ssram_base_addr();
    if (crashlog_addr == 0) {
        LOGE("%s: ssram not available, abort\n", __func__);
        return ret;
    }

    crashlog_region = malloc(CRASHLOG_LENGTH);
    if (!crashlog_region) {
        LOGE("%s: crashlog region malloc failed, requested size: %d\n",
             __func__, CRASHLOG_LENGTH);
        return ret;
    }

    if (read_dev_mem_region(crashlog_addr,
                            CRASHLOG_LENGTH,
                            crashlog_region)) {
        LOGE("%s: crashlog region dump failed : %s (%d)\n",
             __func__, strerror(errno), errno);
        goto free_crashlog_region;
    }

    snprintf(dstfile, sizeof(dstfile), "%s/fwerr_ssram-dump_%s.bin",
             dir, get_current_time_short(0));

    fd = open(dstfile, O_WRONLY | O_CREAT, 0660);
    if (fd == -1) {
        LOGE("%s: Failed to open %s\n", __func__, dstfile);
        goto free_crashlog_region;
    }

    if (write(fd, crashlog_region, CRASHLOG_LENGTH) == -1) {
        LOGE("%s: Failed to write crashlog region to %s : %s (%d)\n",
             __func__, dstfile, strerror(errno), errno);
        goto close;
    }

    ret = 0;

  close:
    close(fd);
    do_chown(dstfile, PERM_USER, PERM_GROUP);
  free_crashlog_region:
    free(crashlog_region);
    return ret;
}


