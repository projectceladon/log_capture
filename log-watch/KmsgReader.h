/*
 * Copyright (C) Intel 2015
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
#ifndef KMSGREADER_H_
#define KMSGREADER_H_

#include <stddef.h>
#include <memory>

#include "LogReader.h"

/* Even if the max log line should be under 1K, the fact that unprintable
 * chars are expanded to '\x##' could get us up to @4k in theory.
 * However devkmsg (kernel/printk/printk.c) implementation has a maximum
 * of 8k so we should go with that as well*/
#define LOG_MAX_LEN 8192

class KmsgReader : public LogReader {
  int fd;
  unsigned char last_prio;
  uint64_t last_timestamp;
  char read_buf[LOG_MAX_LEN];
  bool nonblock;

 public:
  explicit KmsgReader(bool nonblock = false);
  virtual std::shared_ptr<LogItem> get();
  virtual ~KmsgReader();
};

#endif  // KMSGREADER_H_
