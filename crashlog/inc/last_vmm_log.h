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
* Copyright (C) Intel 2016
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * @file last_vmm_log.h
 * @brief File containing functions to save the last vmm log.
 */

#ifndef __LAST_VMM_LOG_H__
#define __LAST_VMM_LOG_H__

#ifdef CONFIG_SOFIA
void crashlog_check_last_vmm_log(void);
#else
static inline void crashlog_check_last_vmm_log(void) {}
#endif
#endif /* __LAST_VMM_LOG_H__ */
