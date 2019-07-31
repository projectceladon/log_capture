/*
 * Copyright (C) Intel 2016
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

#ifndef LOGDREADER_H_
#define LOGDREADER_H_

#include "LogReader.h"
#include <vector>

struct EventTagMap;
struct AndroidLogFormat_t;
typedef struct AndroidLogFormat_t AndroidLogFormat;

class LogdReader : public LogReader {
  std::vector<std::string> buffers;
  EventTagMap *evTags = NULL;
  struct logger_list *logger_list = NULL;
  int mode;

  void addLogBuffer(char const *name);
  int processArguments(std::string args);

 public:
  explicit LogdReader(std::string args);
  virtual std::shared_ptr<LogItem> get();
  virtual ~LogdReader();
};

#endif /* LOGDREADER_H_ */
