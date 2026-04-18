/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * DO NOT ALTER OR REMOVE THIS FILE HEADER.
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

#ifndef SHARE_VM_MATRIX_UBSOCKETFRAME_HPP
#define SHARE_VM_MATRIX_UBSOCKETFRAME_HPP

#include <stdint.h>
#include <sys/socket.h>

#include "matrix/ubSocket/ubSocket.hpp"

// --- Control frame types (attach handshake) ---

enum UBSocketControlType {
  UB_SOCKET_CONTROL_ATTACH_REQ = 1,
  UB_SOCKET_CONTROL_ATTACH_RSP = 2,
  UB_SOCKET_CONTROL_ATTACH_COMMIT = 3,
  UB_SOCKET_CONTROL_ATTACH_ACK = 4
};

struct UBSocketEndpoint {
  uint16_t family;
  uint16_t port;
  uint8_t addr[16];
};

struct UBSocketControlFrame {
  uint16_t type;
  uint16_t success;
  UBSocketEndpoint local_endpoint;
  UBSocketEndpoint remote_endpoint;
  char mem_name[UB_SOCKET_MEM_NAME_BUF_LEN];
};

bool ub_frame_endpoint_equals(const UBSocketEndpoint* lhs,
                              const UBSocketEndpoint* rhs);
bool ub_frame_request_equals(const UBSocketControlFrame* lhs,
                             const UBSocketControlFrame* rhs);
bool ub_frame_endpoint_to_addr(const UBSocketEndpoint* endpoint,
                               struct sockaddr_storage* storage, socklen_t* addr_len);
bool ub_frame_get_endpoints(int fd, UBSocketEndpoint* local_ep,
                            UBSocketEndpoint* remote_ep);
void ub_frame_init(UBSocketControlFrame* frame, uint16_t type);
bool ub_frame_send(int fd, UBSocketControlFrame* frame, uint64_t ddl_ns);
bool ub_frame_recv(int fd, UBSocketControlFrame* frame, uint16_t expected_type,
                   uint64_t ddl_ns);
void ub_frame_fill_endpoints(UBSocketControlFrame* frame,
                             const UBSocketEndpoint* local_ep,
                             const UBSocketEndpoint* remote_ep);

#endif  // SHARE_VM_MATRIX_UBSOCKETFRAME_HPP
