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

#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "matrix/ubSocket/ubSocket.hpp"

static const uint16_t UB_SOCKET_PROTOCOL_VERSION = 803; // JDK 08 + generation-ready layout 03
static const uint32_t UB_SOCKET_OK_CODE = 0;
static const uint32_t UB_SOCKET_ERROR_CODE = 1;
static const size_t UB_SOCKET_PEER_TEXT_BUF_LEN = 128;

enum UBSocketAttachKind {
  UB_SOCKET_ATTACH_REQ = 1,
  UB_SOCKET_ATTACH_RSP = 2,
  UB_SOCKET_ATTACH_COMMIT = 3,
  UB_SOCKET_ATTACH_ACK = 4
};

enum UBSocketControlKind {
  UB_SOCKET_SHUTDOWN_OUTPUT = 1,
  UB_SOCKET_CLOSE = 2
};

enum UBSocketCapability {
  UB_SOCKET_CAP_GENERATION_READY = 1,
  UB_SOCKET_CAP_SPIN_COALESCE = 2
};

struct UBSocketEndpoint {
  uint16_t family;
  uint16_t port;
  uint8_t addr[16];
};

// Wire layout: field order affects padding and sizeof(frame).
// Update protocol version and STATIC_ASSERTs if this changes.
struct UBSocketAttachFrame {
  uint16_t version;
  uint16_t kind;
  uint32_t request_id;
  uint32_t error_code;
  uint32_t capabilities;
  uint32_t ring_count;
  uint32_t connection_generation;
  uint32_t ring_slot;
  uint64_t mapping_size;
  uint64_t ring_memory_size;
  uint64_t spin_nanos;
  uint64_t ring_offset;
  uint64_t ring_size;
  UBSocketEndpoint local_endpoint;
  UBSocketEndpoint remote_endpoint;
  char mem_name[UB_SOCKET_MEM_NAME_BUF_LEN];
};

struct UBSocketControlFrame {
  uint16_t version;
  uint16_t kind;
  uint32_t connection_generation;
  uint64_t final_write_offset;
};

struct UBSocketProtocolInfo {
  uint32_t capabilities;
  uint32_t ring_count;
  uint64_t mapping_size;
  uint64_t ring_memory_size;
  uint64_t spin_nanos;
};

static const int UB_SOCKET_ATTACH_FRAME_WIRE_SIZE = sizeof(UBSocketAttachFrame);
static const int UB_SOCKET_CONTROL_FRAME_WIRE_SIZE = sizeof(UBSocketControlFrame);

UBSocketAttachFrame ub_socket_attach_frame(uint16_t kind,
                                           uint32_t request_id,
                                           uint32_t error_code,
                                           const UBSocketEndpoint* local_ep,
                                           const UBSocketEndpoint* remote_ep,
                                           const char* mem_name,
                                           const UBSocketProtocolInfo& protocol,
                                           uint32_t connection_generation = 0,
                                           uint32_t ring_slot = 0,
                                           uint64_t ring_offset = 0,
                                           uint64_t ring_size = 0);

UBSocketProtocolInfo ub_socket_protocol_info(uint64_t mapping_size,
                                             uint64_t ring_memory_size,
                                             uint32_t ring_count,
                                             uint64_t spin_nanos);
bool ub_socket_attach_compatible(const UBSocketAttachFrame& frame,
                                 const UBSocketProtocolInfo& protocol,
                                 bool require_ring);

UBSocketControlFrame ub_socket_control_frame(uint16_t kind,
                                             uint32_t connection_generation,
                                             uint64_t final_write_offset);

bool ub_socket_attach_send(int fd, const UBSocketAttachFrame& frame, uint64_t ddl_ns);
bool ub_socket_attach_recv(int fd, UBSocketAttachFrame* frame,
                           uint16_t expected_kind, uint64_t ddl_ns);
ssize_t ub_socket_control_send(int fd, const UBSocketControlFrame& frame,
                               size_t* bytes_sent = NULL);
bool ub_socket_control_parse(const void* raw, UBSocketControlFrame* frame);

bool ub_socket_endpoint_equals(const UBSocketEndpoint* lhs,
                               const UBSocketEndpoint* rhs);
bool ub_socket_endpoint_to_addr(const UBSocketEndpoint* endpoint,
                                struct sockaddr_storage* storage, socklen_t* addr_len);
bool ub_socket_endpoint_get(int fd, UBSocketEndpoint* local_ep,
                            UBSocketEndpoint* remote_ep);
void ub_socket_peer_to_string(int fd, char* buf, size_t len);

#endif  // SHARE_VM_MATRIX_UBSOCKETFRAME_HPP
