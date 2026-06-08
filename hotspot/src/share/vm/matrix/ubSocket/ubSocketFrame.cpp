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

#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static const size_t UB_SOCKET_IPV4_ADDR_LEN = sizeof(struct in_addr);
static const size_t UB_SOCKET_IPV4_MAPPED_OFFSET =
    sizeof(struct in6_addr) - sizeof(struct in_addr);

static bool ub_socket_is_attach_kind(uint16_t kind) {
  return kind == UB_SOCKET_ATTACH_REQ ||
         kind == UB_SOCKET_ATTACH_RSP ||
         kind == UB_SOCKET_ATTACH_COMMIT ||
         kind == UB_SOCKET_ATTACH_ACK;
}

static bool ub_socket_is_wakeup_kind(uint16_t kind) {
  return kind == UB_SOCKET_WAKEUP || kind == UB_SOCKET_CLOSE;
}

static void ub_socket_copy_mem_name(char* dst, const char* src) {
  memset(dst, 0, UB_SOCKET_MEM_NAME_BUF_LEN);
  if (src != NULL) {
    strncpy(dst, src, UB_SOCKET_MEM_NAME_LEN);
  }
}

static void ub_socket_encode_wakeup_frame(const UBSocketWakeupFrame& frame, void* raw) {
  memcpy(raw, &frame, UB_SOCKET_WAKEUP_FRAME_WIRE_SIZE);
}

static void ub_socket_decode_wakeup_frame(const void* raw, UBSocketWakeupFrame* frame) {
  memcpy(frame, raw, UB_SOCKET_WAKEUP_FRAME_WIRE_SIZE);
}

static bool ub_socket_verify_attach_frame(UBSocketAttachFrame* frame,
                                          uint16_t expected_kind) {
  if (frame->version != UB_SOCKET_PROTOCOL_VERSION ||
      !ub_socket_is_attach_kind(frame->kind) ||
      frame->kind != expected_kind ||
      frame->mem_name[UB_SOCKET_MEM_NAME_LEN] != '\0') {
    errno = EBADMSG;
    return false;
  }
  return true;
}

static bool ub_socket_verify_wakeup_frame(UBSocketWakeupFrame* frame) {
  if (!ub_socket_is_wakeup_kind(frame->kind)) {
    errno = EBADMSG;
    return false;
  }
  return true;
}

bool ub_socket_endpoint_equals(const UBSocketEndpoint* lhs,
                               const UBSocketEndpoint* rhs) {
  if ((int)lhs->port != (int)rhs->port) { return false; }
  if (lhs->family == rhs->family) {
    return memcmp(lhs->addr, rhs->addr, sizeof(lhs->addr)) == 0;
  }
  if (lhs->family == AF_INET && rhs->family == AF_INET6 &&
      IN6_IS_ADDR_V4MAPPED((const struct in6_addr*)rhs->addr)) {
    return memcmp(lhs->addr, rhs->addr + UB_SOCKET_IPV4_MAPPED_OFFSET,
                  UB_SOCKET_IPV4_ADDR_LEN) == 0;
  }
  if (lhs->family == AF_INET6 && rhs->family == AF_INET &&
      IN6_IS_ADDR_V4MAPPED((const struct in6_addr*)lhs->addr)) {
    return memcmp(lhs->addr + UB_SOCKET_IPV4_MAPPED_OFFSET, rhs->addr,
                  UB_SOCKET_IPV4_ADDR_LEN) == 0;
  }
  return false;
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

void ub_socket_peer_to_string(int fd, char* buf, size_t len) {
  if (buf == NULL || len == 0) { return; }
  jio_snprintf(buf, len, "<unknown>");

  struct sockaddr_storage addr;
  socklen_t addr_len = sizeof(addr);
  if (getpeername(fd, (struct sockaddr*)&addr, &addr_len) != 0) {
    return;
  }

  char ip[INET6_ADDRSTRLEN];
  if (addr.ss_family == AF_INET) {
    const struct sockaddr_in* in = (const struct sockaddr_in*)&addr;
    if (inet_ntop(AF_INET, &in->sin_addr, ip, sizeof(ip)) != NULL) {
      jio_snprintf(buf, len, "%s:%u", ip, ntohs(in->sin_port));
    }
    return;
  }
  if (addr.ss_family == AF_INET6) {
    const struct sockaddr_in6* in6 = (const struct sockaddr_in6*)&addr;
    if (inet_ntop(AF_INET6, &in6->sin6_addr, ip, sizeof(ip)) != NULL) {
      jio_snprintf(buf, len, "[%s]:%u", ip, ntohs(in6->sin6_port));
    }
  }
}

UBSocketAttachFrame ub_socket_attach_frame(uint16_t kind,
                                           uint32_t request_id,
                                           uint32_t error_code,
                                           const UBSocketEndpoint* local_ep,
                                           const UBSocketEndpoint* remote_ep,
                                           const char* mem_name,
                                           uint32_t ring_slot,
                                           uint64_t ring_offset,
                                           uint64_t ring_size) {
  UBSocketAttachFrame frame;
  memset(&frame, 0, sizeof(frame));
  frame.version = UB_SOCKET_PROTOCOL_VERSION;
  frame.kind = kind;
  frame.request_id = request_id;
  frame.error_code = error_code;
  frame.ring_slot = ring_slot;
  frame.ring_offset = ring_offset;
  frame.ring_size = ring_size;
  if (local_ep != NULL) {
    frame.local_endpoint = *local_ep;
  }
  if (remote_ep != NULL) {
    frame.remote_endpoint = *remote_ep;
  }
  ub_socket_copy_mem_name(frame.mem_name, mem_name);
  return frame;
}

UBSocketWakeupFrame ub_socket_wakeup_frame(uint16_t kind) {
  UBSocketWakeupFrame frame;
  memset(&frame, 0, sizeof(frame));
  frame.kind = kind;
  return frame;
}

bool ub_socket_attach_send(int fd, const UBSocketAttachFrame& frame, uint64_t ddl_ns) {
  return UBSocketIO::send_all(fd, &frame, UB_SOCKET_ATTACH_FRAME_WIRE_SIZE,
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

ssize_t ub_socket_wakeup_send(int fd, const UBSocketWakeupFrame& frame,
                              size_t* bytes_sent) {
  size_t bytes = UB_SOCKET_WAKEUP_FRAME_WIRE_SIZE;
  UBSocketProfileScope total_profile(UB_PROF_WAKEUP_SEND_TOTAL, bytes);
  uint64_t ddl_ns = (uint64_t)os::javaTimeNanos() +
                    UB_WAKEUP_SEND_TIMEOUT_MS * NANOSECS_PER_MILLISEC;
  char wire_frame[UB_SOCKET_WAKEUP_FRAME_WIRE_SIZE];
  ub_socket_encode_wakeup_frame(frame, wire_frame);
  return UBSocketIO::send_all(fd, wire_frame, bytes, ddl_ns, MSG_NOSIGNAL,
                              bytes_sent, UB_PROF_WAKEUP_SEND_SYSCALL);
}

bool ub_socket_wakeup_parse(const void* raw, UBSocketWakeupFrame* frame) {
  memset(frame, 0, sizeof(*frame));
  ub_socket_decode_wakeup_frame(raw, frame);
  return ub_socket_verify_wakeup_frame(frame);
}
