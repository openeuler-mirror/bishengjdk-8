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
#include "matrix/ubSocket/ubSocketIO.hpp"

#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

bool ub_frame_endpoint_equals(const UBSocketEndpoint* lhs,
                              const UBSocketEndpoint* rhs) {
  if (lhs->family != rhs->family) { return false; }
  int addr_cmp = memcmp(lhs->addr, rhs->addr, sizeof(lhs->addr));
  if (addr_cmp != 0) { return false; }
  return (int)lhs->port == (int)rhs->port;
}

bool ub_frame_request_equals(const UBSocketControlFrame* lhs,
                             const UBSocketControlFrame* rhs) {
  return lhs->type == rhs->type &&
         strncmp(lhs->mem_name, rhs->mem_name, UB_SOCKET_MEM_NAME_BUF_LEN) == 0 &&
         ub_frame_endpoint_equals(&lhs->local_endpoint, &rhs->local_endpoint) &&
         ub_frame_endpoint_equals(&lhs->remote_endpoint, &rhs->remote_endpoint);
}

static bool ub_frame_endpoint_from_addr(const struct sockaddr_storage* storage,
                                        UBSocketEndpoint* endpoint) {
  memset(endpoint, 0, sizeof(*endpoint));
  if (storage->ss_family == AF_INET) {
    const struct sockaddr_in* in = (const struct sockaddr_in*)storage;
    endpoint->family = AF_INET;
    endpoint->port = ntohs(in->sin_port);
    memcpy(endpoint->addr, &in->sin_addr, sizeof(in->sin_addr));
    return true;
  }
  if (storage->ss_family == AF_INET6) {
    const struct sockaddr_in6* in6 = (const struct sockaddr_in6*)storage;
    endpoint->family = AF_INET6;
    endpoint->port = ntohs(in6->sin6_port);
    memcpy(endpoint->addr, &in6->sin6_addr, sizeof(in6->sin6_addr));
    return true;
  }
  errno = EAFNOSUPPORT;
  return false;
}

bool ub_frame_endpoint_to_addr(const UBSocketEndpoint* endpoint,
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

bool ub_frame_get_endpoints(int fd, UBSocketEndpoint* local_ep,
                            UBSocketEndpoint* remote_ep) {
  struct sockaddr_storage local_addr;
  struct sockaddr_storage remote_addr;
  socklen_t local_len = sizeof(local_addr);
  socklen_t remote_len = sizeof(remote_addr);
  if (getsockname(fd, (struct sockaddr*)&local_addr, &local_len) != 0 ||
      getpeername(fd, (struct sockaddr*)&remote_addr, &remote_len) != 0) {
    return false;
  }
  return ub_frame_endpoint_from_addr(&local_addr, local_ep) &&
         ub_frame_endpoint_from_addr(&remote_addr, remote_ep);
}

void ub_frame_init(UBSocketControlFrame* frame, uint16_t type) {
  memset(frame, 0, sizeof(*frame));
  frame->type = type;
}

bool ub_frame_send(int fd, UBSocketControlFrame* frame, uint64_t ddl_ns) {
  return UBSocketIO::send_all(fd, frame, sizeof(*frame), ddl_ns, MSG_NOSIGNAL);
}

bool ub_frame_recv(int fd, UBSocketControlFrame* frame, uint16_t expected_type,
                   uint64_t ddl_ns) {
  if (!UBSocketIO::recv_all(fd, frame, sizeof(*frame), ddl_ns, 0)) {
    return false;
  }
  if (frame->type != expected_type) {
    errno = EBADMSG;
    return false;
  }
  return true;
}

void ub_frame_fill_endpoints(UBSocketControlFrame* frame,
                             const UBSocketEndpoint* local_ep,
                             const UBSocketEndpoint* remote_ep) {
  frame->local_endpoint = *local_ep;
  frame->remote_endpoint = *remote_ep;
}
