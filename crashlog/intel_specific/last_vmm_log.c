/* Copyright (C) Intel 2016
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

/**
 * @file last_vmm_log.c
 * @brief File containing functions used to save the last vmm log.
 * Save the last vmm log to file /data/logs/last_vmm_log.
 */

#include "fsutils.h"
#include "crashutils.h"
#include "privconfig.h"

#define LAST_VMM_LOG_PROC   PROC_DIR "/" LAST_VMM_LOG

void crashlog_check_last_vmm_log(void)
{
    remove(LAST_VMM_LOG_FILE);

    if (file_exists(LAST_VMM_LOG_PROC))
        do_copy_eof(LAST_VMM_LOG_PROC, LAST_VMM_LOG_FILE);
}
