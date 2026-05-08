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

#include "matrix/ubSocket/ubSocketFrame.hpp"
#include "matrix/matrixLog.hpp"
#include "matrix/ubSocket/ubSocketIO.hpp"
#include "matrix/ubSocket/ubSocketProfile.hpp"
#include "memory/resourceArea.hpp"

#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static bool ub_socket_is_attach_kind(uint16_t kind) {
  return kind == UB_SOCKET_ATTACH_REQ ||
         kind == UB_SOCKET_ATTACH_RSP ||
         kind == UB_SOCKET_ATTACH_COMMIT ||
         kind == UB_SOCKET_ATTACH_ACK;
}

static bool ub_socket_is_data_kind(uint16_t kind) {
  return kind == UB_SOCKET_DATA_DESCRIPTOR ||
         kind == UB_SOCKET_DATA_HEARTBEAT ||
         kind == UB_SOCKET_DATA_FALLBACK;
}

static void ub_socket_copy_mem_name(char* dst, const char* src) {
  memset(dst, 0, UB_SOCKET_MEM_NAME_BUF_LEN);
  if (src != NULL) {
    strncpy(dst, src, UB_SOCKET_MEM_NAME_LEN);
  }
}

static void ub_socket_copy_mem_name(char* dst, const Symbol* src) {
  memset(dst, 0, UB_SOCKET_MEM_NAME_BUF_LEN);
  if (src != NULL) {
    int len = src->utf8_length();
    if (len > UB_SOCKET_MEM_NAME_LEN) {
      len = UB_SOCKET_MEM_NAME_LEN;
    }
    memcpy(dst, src->bytes(), len);
  }
}

static UBSocketAttachFrame ub_socket_wire_attach_frame(const UBSocketAttachFrame& frame) {
  UBSocketAttachFrame wire;
  memset(&wire, 0, sizeof(wire));
  wire.version = frame.version;
  wire.kind = frame.kind;
  wire.request_id = frame.request_id;
  wire.checksum = 0;
  wire.error_code = frame.error_code;
  wire.local_endpoint = frame.local_endpoint;
  wire.remote_endpoint = frame.remote_endpoint;
  memcpy(wire.mem_name, frame.mem_name, UB_SOCKET_MEM_NAME_BUF_LEN);
  // Checksum is reserved for future protocol hardening; current wire value is 0.
  wire.checksum = 0;
  return wire;
}

static UBSocketDataFrame ub_socket_wire_data_frame(const UBSocketDataFrame& frame) {
  UBSocketDataFrame wire;
  memset(&wire, 0, sizeof(wire));
  wire.offset = frame.offset;
  wire.length = frame.length;
  wire.checksum = 0;
  wire.version = frame.version;
  wire.kind = frame.kind;
  memcpy(wire.mem_name, frame.mem_name, UB_SOCKET_MEM_NAME_BUF_LEN);
  // Checksum is reserved for future protocol hardening; current wire value is 0.
  wire.checksum = 0;
  return wire;
}

static bool ub_socket_verify_attach_frame(UBSocketAttachFrame* frame,
                                          uint16_t expected_kind) {
  if (frame->version != UB_SOCKET_PROTOCOL_VERSION ||
      !ub_socket_is_attach_kind(frame->kind) ||
      frame->kind != expected_kind ||
      frame->checksum != 0 ||
      frame->mem_name[UB_SOCKET_MEM_NAME_LEN] != '\0') {
    errno = EBADMSG;
    return false;
  }
  return true;
}

static bool ub_socket_verify_data_frame(UBSocketDataFrame* frame) {
  if (frame->version != UB_SOCKET_PROTOCOL_VERSION ||
      !ub_socket_is_data_kind(frame->kind) ||
      frame->checksum != 0 ||
      frame->mem_name[UB_SOCKET_MEM_NAME_LEN] != '\0') {
    errno = EBADMSG;
    return false;
  }
  return true;
}

bool ub_socket_endpoint_equals(const UBSocketEndpoint* lhs,
                               const UBSocketEndpoint* rhs) {
  if (lhs->family != rhs->family) { return false; }
  if (memcmp(lhs->addr, rhs->addr, sizeof(lhs->addr)) != 0) { return false; }
  return (int)lhs->port == (int)rhs->port;
}

static bool ub_socket_endpoint_from_addr(const struct sockaddr_storage* storage,
                                         UBSocketEndpoint* endpoint) {
  memset(endpoint, 0, sizeof(*endpoint));
  if (storage->ss_family == AF_INET) {
    const struct sockaddr_in* in = (const struct sockaddr_in*)storage;
    endpoint->family = AF_INET;
    endpoint->port = ntohs(in->sin_port);
    memcpy(endpoint->addr, &in->sin_addr, sizeof(in->sin_addr));
    char ip[INET_ADDRSTRLEN];
    const char* addr = inet_ntop(AF_INET, endpoint->addr, ip, sizeof(ip));
    UB_LOG(UB_SOCKET, UB_LOG_INFO, "IPv4 endpoint init ip=%s port=%u\n",
           addr == NULL ? "<invalid>" : addr, endpoint->port);
    return true;
  }
  if (storage->ss_family == AF_INET6) {
    const struct sockaddr_in6* in6 = (const struct sockaddr_in6*)storage;
    endpoint->family = AF_INET6;
    endpoint->port = ntohs(in6->sin6_port);
    memcpy(endpoint->addr, &in6->sin6_addr, sizeof(in6->sin6_addr));
    char ip[INET6_ADDRSTRLEN];
    const char* addr = inet_ntop(AF_INET6, endpoint->addr, ip, sizeof(ip));
    UB_LOG(UB_SOCKET, UB_LOG_INFO, "IPv6 endpoint init ip=%s port=%u\n",
           addr == NULL ? "<invalid>" : addr, endpoint->port);
    return true;
  }
  errno = EAFNOSUPPORT;
  return false;
}

bool ub_socket_endpoint_to_addr(const UBSocketEndpoint* endpoint,
                                struct sockaddr_storage* storage, socklen_t* addr_len) {
  memset(storage, 0, sizeof(*storage));
  if (endpoint->family == AF_INET) {
    struct sockaddr_in* in = (struct sockaddr_in*)storage;
    in->sin_family = AF_INET;
    in->sin_port = htons(endpoint->port);
    memcpy(&in->sin_addr, endpoint->addr, sizeof(in->sin_addr));
    *addr_len = sizeof(*in);
    return true;
  }
  if (endpoint->family == AF_INET6) {
    struct sockaddr_in6* in6 = (struct sockaddr_in6*)storage;
    in6->sin6_family = AF_INET6;
    in6->sin6_port = htons(endpoint->port);
    memcpy(&in6->sin6_addr, endpoint->addr, sizeof(in6->sin6_addr));
    *addr_len = sizeof(*in6);
    return true;
  }
  errno = EAFNOSUPPORT;
  return false;
}

bool ub_socket_endpoint_get(int fd, UBSocketEndpoint* local_ep,
                            UBSocketEndpoint* remote_ep) {
  struct sockaddr_storage local_addr;
  struct sockaddr_storage remote_addr;
  socklen_t local_len = sizeof(local_addr);
  socklen_t remote_len = sizeof(remote_addr);
  if (getsockname(fd, (struct sockaddr*)&local_addr, &local_len) != 0 ||
      getpeername(fd, (struct sockaddr*)&remote_addr, &remote_len) != 0) {
    return false;
  }
  return ub_socket_endpoint_from_addr(&local_addr, local_ep) &&
         ub_socket_endpoint_from_addr(&remote_addr, remote_ep);
}

UBSocketAttachFrame ub_socket_attach_frame(uint16_t kind,
                                           uint32_t request_id,
                                           uint32_t error_code,
                                           const UBSocketEndpoint* local_ep,
                                           const UBSocketEndpoint* remote_ep,
                                           const char* mem_name) {
  UBSocketAttachFrame frame;
  memset(&frame, 0, sizeof(frame));
  frame.version = UB_SOCKET_PROTOCOL_VERSION;
  frame.kind = kind;
  frame.request_id = request_id;
  frame.error_code = error_code;
  if (local_ep != NULL) {
    frame.local_endpoint = *local_ep;
  }
  if (remote_ep != NULL) {
    frame.remote_endpoint = *remote_ep;
  }
  ub_socket_copy_mem_name(frame.mem_name, mem_name);
  return frame;
}

UBSocketDataFrame ub_socket_data_frame(uint16_t kind,
                                       const char* mem_name,
                                       uint64_t offset,
                                       uint64_t length) {
  UBSocketDataFrame frame;
  memset(&frame, 0, sizeof(frame));
  frame.offset = offset;
  frame.length = length;
  frame.version = UB_SOCKET_PROTOCOL_VERSION;
  frame.kind = kind;
  ub_socket_copy_mem_name(frame.mem_name, mem_name);
  return frame;
}

UBSocketDataFrame ub_socket_data_frame(uint16_t kind,
                                       const Symbol* mem_name,
                                       uint64_t offset,
                                       uint64_t length) {
  UBSocketDataFrame frame;
  memset(&frame, 0, sizeof(frame));
  frame.offset = offset;
  frame.length = length;
  frame.version = UB_SOCKET_PROTOCOL_VERSION;
  frame.kind = kind;
  ub_socket_copy_mem_name(frame.mem_name, mem_name);
  return frame;
}

bool ub_socket_attach_send(int fd, const UBSocketAttachFrame& frame, uint64_t ddl_ns) {
  UBSocketAttachFrame wire = ub_socket_wire_attach_frame(frame);
  return UBSocketIO::send_all(fd, &wire, UB_SOCKET_ATTACH_FRAME_WIRE_SIZE,
                              ddl_ns, MSG_NOSIGNAL) ==
         UB_SOCKET_ATTACH_FRAME_WIRE_SIZE;
}

bool ub_socket_attach_recv(int fd, UBSocketAttachFrame* frame,
                           uint16_t expected_kind, uint64_t ddl_ns) {
  memset(frame, 0, sizeof(*frame));
  if (!UBSocketIO::recv_all(fd, frame, UB_SOCKET_ATTACH_FRAME_WIRE_SIZE, ddl_ns, 0)) {
    return false;
  }
  return ub_socket_verify_attach_frame(frame, expected_kind);
}

ssize_t ub_socket_data_send(int fd, const UBSocketDataFrame& frame,
                            size_t* bytes_sent) {
  size_t bytes = UB_SOCKET_DATA_FRAME_WIRE_SIZE;
  UBSocketProfileScope total_profile(UB_PROF_DESCRIPTOR_SEND_TOTAL, bytes);
  uint64_t ddl_ns = (uint64_t)os::javaTimeNanos() +
                    UB_DATA_FRAME_SEND_TIMEOUT_MS * NANOSECS_PER_MILLISEC;
  UBSocketDataFrame wire_frame = ub_socket_wire_data_frame(frame);
  return UBSocketIO::send_all(fd, &wire_frame, bytes, ddl_ns, MSG_NOSIGNAL,
                              bytes_sent, UB_PROF_DESCRIPTOR_SEND_SYSCALL);
}

bool ub_socket_data_parse(const void* raw, UBSocketDataFrame* frame) {
  memset(frame, 0, sizeof(*frame));
  memcpy(frame, raw, UB_SOCKET_DATA_FRAME_WIRE_SIZE);
  return ub_socket_verify_data_frame(frame);
}
