/* Copyright (C) Intel 2019
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
 * @file check_partition.h
 * @brief File containing functions for getting user space events.
 */

#ifndef __CHECK_PARTITION_H__
#define __CHECK_PARTITION_H__

#ifdef CONFIG_PARTITIONS_CHECK
int check_mounted_partitions(void);
#else
static inline int check_mounted_partitions(void) { return 0; }
#endif

#ifdef CONFIG_FACTORY_CHECKSUM
void check_factory_partition_checksum();
#else
static inline void check_factory_partition_checksum() {}
#endif

#endif /* __CHECK_PARTITION_H__ */
