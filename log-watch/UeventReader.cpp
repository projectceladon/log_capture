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

#include "UeventReader.h"

#include <sys/socket.h>
#include <linux/netlink.h>
#include <unistd.h>
#include <string.h>

#include "LwLog.h"

/*Priority assignment*/
#define SRC_KERNEL 0
#define SRC_USPACE 1
#define SRC_UNKNOWN 2

UeventReader::UeventReader(bool nonblock) {
  struct sockaddr_nl addr;
  int buf_sz = getpagesize();
  int on = 1;

  this->nonblock = nonblock;
  memset(&addr, 0, sizeof(addr));
  addr.nl_family = AF_NETLINK;
  addr.nl_pid = getpid();
  addr.nl_groups = 0xffffffff;

  fd = socket(PF_NETLINK, SOCK_RAW, NETLINK_KOBJECT_UEVENT);
  if (fd < 0) {
    LwLog::error("Cannot open NETLINK_KOBJECT_UEVENT (%d)", fd);
  } else {
    setsockopt(fd, SOL_SOCKET, SO_RCVBUFFORCE, &buf_sz, sizeof(buf_sz));
    setsockopt(fd, SOL_SOCKET, SO_PASSCRED, &on, sizeof(on));
    if (bind(fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
      close(fd);
      LwLog::error("Unable to bind NETLINK_KOBJECT_UEVENT (%d)", fd);
      fd = -1;
    }
  }
}

std::shared_ptr<LogItem> UeventReader::get() {
  std::shared_ptr<LogItem> ret = std::make_shared<LogItem>();

  if (!ret)
    LwLog::critical("Cannot allocate log item");
  if (fd < 0) {
    ret->setEof(true);
    return ret;
  }

  size_t d_len = 4096;
  char buf[4096];
  struct iovec iov = { buf, d_len };
  struct sockaddr_nl addr;
  char control[CMSG_SPACE(sizeof(struct ucred))];
  struct msghdr msgh = {
      &addr,
      sizeof(addr),
      &iov,
      1,
      control,
      sizeof(control),
      0,
  };

  int flags = nonblock ? MSG_DONTWAIT : 0;
  int len = recvmsg(fd, &msgh, flags);

  if (len <= 0) {
    ret->setEof(true);
    return ret;
  }

  unsigned char prio = SRC_UNKNOWN;

  struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msgh);
  if (cmsg != NULL && cmsg->cmsg_type == SCM_CREDENTIALS) {
    struct ucred *cred = (struct ucred *) CMSG_DATA(cmsg);
    if (cred->pid) {
      prio = SRC_USPACE;
    } else {
      prio = SRC_KERNEL;
    }
  }

  for (int i = 0; i < len; i++)
    if (buf[i] == 0)
      buf[i] = '\n';

  /*Keep it null terminated*/
  buf[len-1] = 0;

  /*Set the priority*/
  ret->setPrio(prio);

  char *msg = new char[len];
  if (msg) {
    memcpy(msg, buf, len);
  }
  ret->setTimestamp(TimeVal::current());
  ret->setMsg(msg);

  return ret;
}

UeventReader::~UeventReader() {
  if (fd > 0)
    close(fd);
}

