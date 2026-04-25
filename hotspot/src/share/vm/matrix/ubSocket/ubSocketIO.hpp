/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef SHARE_VM_MATRIX_UBSOCKETIO_HPP
#define SHARE_VM_MATRIX_UBSOCKETIO_HPP

#include <stdint.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "memory/allocation.hpp"

class UBSocketIO : public AllStatic {
 public:
  static bool is_retryable_error(int error_code);
  static bool wait_fd(int fd, short events, uint64_t ddl_ns);
  static int connect(int family, const struct sockaddr* addr,
                     socklen_t addr_len, uint64_t ddl_ns);
  static int accept(int listen_fd);

  static ssize_t read(int fd, void* buf, size_t len);
  static ssize_t write(int fd, const void* buf, size_t len);
  static ssize_t send(int fd, const void* buf, size_t len, int flags);
  static ssize_t recv(int fd, void* buf, size_t len, int flags);

  static ssize_t send_all(int fd, const void* buf, size_t len,
                          uint64_t ddl_ns, int flags, size_t* bytes_sent = NULL);
  static bool recv_all(int fd, void* buf, size_t len,
                       uint64_t ddl_ns, int flags);
};

#endif  // SHARE_VM_MATRIX_UBSOCKETIO_HPP
