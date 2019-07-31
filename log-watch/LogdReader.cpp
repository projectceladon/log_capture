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

#include "LogdReader.h"
#include "LwLog.h"
#include "log/log.h"

#include <fcntl.h>
#include <getopt.h>
#include <log/logprint.h>
#include <sstream>
#include <string.h>

#define NSEC_IN_USEC 1000L
#define NSEC_TO_USEC(a) ((a)/(NSEC_IN_USEC))

void LogdReader::addLogBuffer(const char *name) {
  log_id_t buffer = android_name_to_log_id(name);
  if (buffer < LOG_ID_MIN || buffer >= LOG_ID_MAX) {
    LwLog::critical("Unknown buffer %s\n", name);
    return;
  }
  if ((buffer == LOG_ID_EVENTS || buffer == LOG_ID_SECURITY) && evTags == NULL) {
    evTags = android_openEventTagMap(EVENT_TAG_MAP_FILE);
  }
  if (!android_logger_open(logger_list, buffer)) {
    LwLog::critical("Could not open logger %s\n", name);
    return;
  }
}

int LogdReader::processArguments(std::string args) {
  int maxArgs = 64;
  int argc = 0;
  char *argv[maxArgs];
  char *cmd = strdup(args.c_str());
  int opt, option_index = 0;
  static const struct option long_options[] = {
    { "nonblock",      no_argument,       NULL,   'd' },
    { NULL,            0,                 NULL,   0 }
  };

  char separator[2]= " ";
  char *p2 = strtok(cmd, separator);
  while (p2 && argc < maxArgs-1) {
    argv[argc++] = p2;
    p2 = strtok(NULL, separator);
  }

  while ((opt = getopt_long(argc, argv, "db:",
         long_options, &option_index)) != -1) {
    switch (opt) {
      case 'd':
        mode |= ANDROID_LOG_NONBLOCK;
        break;
      case 'b':
        if (!strcmp("all", optarg)) {
          for (int i = LOG_ID_MIN; i < LOG_ID_MAX; ++i) {
            const char *name = android_log_id_to_name((log_id_t)i);
            log_id_t log_id = android_name_to_log_id(name);
            if (log_id != (log_id_t)i) {
              continue;
            }
            buffers.push_back(std::string(name));
          }
        }
        else {
          buffers.push_back(std::string(optarg));
        }
        break;
      default:
        delete[] cmd;
        return -1;
    }
  }
  delete[] cmd;
  return 0;
}

LogdReader::LogdReader(std::string args) {
  mode = ANDROID_LOG_RDONLY;
  if (processArguments("log-watch " + args)) {
    LwLog::critical("Problem while parsing arguments\n");
  }

  logger_list = android_logger_list_alloc(mode, 0, 0);
  if (!logger_list) {
    LwLog::critical("Cannot allocate logger list\n");
  }

  if (buffers.empty()) {
    buffers.push_back(std::string("main"));
  }

  for (const auto& buffer : buffers) {
    addLogBuffer(buffer.c_str());
  }
}

std::shared_ptr<LogItem> LogdReader::get() {
  std::shared_ptr<LogItem> logItem = std::make_shared<LogItem>();

  if (!logItem) {
    LwLog::critical("Cannot allocate log item");
  }

  struct log_msg log_msg;
  int ret = android_logger_list_read(logger_list, &log_msg);

  if (ret <= 0) {
    LwLog::error("Unexpected read result %d\n", ret);
    logItem->setEof(true);
    return logItem;
  }

  AndroidLogEntry entry;
  if (log_msg.entry.lid == LOG_ID_EVENTS || log_msg.entry.lid == LOG_ID_SECURITY) {
    char buffer[1024];
    if (android_log_processBinaryLogBuffer(&log_msg.entry_v1, &entry,
      evTags, buffer, sizeof(buffer)) < 0) {
      LwLog::error("Unable to decode binary message\n");
      logItem->setEmpty(true);
      return logItem;
    }
  }
  else if (android_log_processLogBuffer(&log_msg.entry_v1, &entry) < 0) {
    LwLog::error("Unable to process log buffer\n");
    logItem->setEmpty(true);
    return logItem;
  }

  char *outBuffer;
  int len = strlen(entry.tag) + entry.messageLen + 3 * sizeof(char);
  outBuffer = new char[len];
  if (outBuffer) {
    snprintf(outBuffer, len, "%s: %s", entry.tag, entry.message);
  } else {
    LwLog::critical("Cannot allocate entry");
  }

  TimeVal timestamp(entry.tv_sec, NSEC_TO_USEC(entry.tv_nsec));
  logItem->setTimestamp(timestamp);
  logItem->setPrio(entry.priority);
  logItem->setMsg(outBuffer);

  return logItem;
}

LogdReader::~LogdReader() {
  if (logger_list != NULL) {
    android_logger_list_free(logger_list);
  }
}
