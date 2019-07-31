/* Copyright (C) Intel 2018
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
 * @file intel_ecc_handler.h
 * @brief File containing functions to handle the crashlogd files/directories
 * watcher.
 *
 */

#ifndef __INTEL_ECC_HANDLER_H__
#define __INTEL_ECC_HANDLER_H__

#ifdef CONFIG_ECC
void free_ecc_list();
void init_ecc_handler();
void ecc_count_handle();
void restore_count();
void fatal_ecc_err_check();
#endif

#endif /*__INTEL_ECC_HANDLER_H__*/
