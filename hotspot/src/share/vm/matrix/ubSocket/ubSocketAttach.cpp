/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES FROM THIS FILE HEADER.
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

#include "matrix/ubSocket/ubSocketAttach.hpp"
#include "matrix/ubSocket/ubSocketAttachSession.hpp"
#include "matrix/ubSocket/ubSocketDataInfo.hpp"
#include "matrix/ubSocket/ubSocketFrame.hpp"
#include "matrix/ubSocket/ubSocketIO.hpp"
#include "matrix/ubSocket/ubSocketMemMapping.hpp"
#include "matrix/ubSocket/ubSocketProfile.hpp"
#include "matrix/ubSocket/ubSocketRing.hpp"
#include "matrix/ubSocket/ubSocket.hpp"
#include "matrix/ubSocket/ubSocketUtils.hpp"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "matrix/matrixLog.hpp"

UBSocketAttach::UBSocketAttach(int fd, bool is_server, Symbol* local_mem_name, size_t mem_size)
  : _socket_fd(fd),
    _is_server(is_server),
    _local_mem_name(local_mem_name),
    _mem_size(mem_size) {}

static void ub_socket_abort_committed_attach(int fd, bool bind_ok) {
  if (bind_ok) { UBSocketManager::detach_fd(fd); }
  (void)shutdown(fd, SHUT_RDWR);
}

int32_t UBSocketAttach::do_attach() {
  return _is_server ? attach_server() : attach_client();
}

int32_t UBSocketAttach::attach_server() {
  if (!UBSocketAttachAgent::start()) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "Start agent failed: %s\n", strerror(errno));
    return UB_SOCKET_REGISTER_FALLBACK;
  }

  UBSocketEndpoint local_ep;
  UBSocketEndpoint remote_ep;
  if (!ub_socket_endpoint_get(_socket_fd, &local_ep, &remote_ep)) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "fd=%d attach server collect endpoints failed: %s\n",
           _socket_fd, strerror(errno));
    return UB_SOCKET_REGISTER_FALLBACK;
  }

  ResourceMark rm;
  uint64_t local_ring_offset = 0;
  uint64_t local_ring_size = 0;
  uint32_t local_ring_slot = UBSocketRingSlots::alloc(&local_ring_offset, &local_ring_size);
  if (local_ring_slot == UB_SOCKET_RING_INVALID_SLOT) {
    UB_PROFILE_COUNT(UB_PROF_RING_ATTACH_NO_SLOT, 0);
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "fd=%d attach server no local ring slot\n", _socket_fd);
    return UB_SOCKET_REGISTER_FALLBACK;
  }
  UBSocketAttachSession* session =
      new UBSocketAttachSession(&local_ep, &remote_ep, _local_mem_name->as_C_string(),
                                local_ring_slot, local_ring_offset, local_ring_size);
  UBSocketSessionCaches::add(session);

  uint64_t now_ns = (uint64_t)os::javaTimeNanos();
  uint64_t request_ddl_ns = now_ns +
                         (uint64_t)UB_ATTACH_REQUEST_WAIT_MS * NANOSECS_PER_MILLISEC;
  uint64_t ddl_ns = now_ns + (uint64_t)UB_ATTACH_TIMEOUT_MS * NANOSECS_PER_MILLISEC;
  int32_t result = UB_SOCKET_REGISTER_FALLBACK;
  char client_mem_name[UB_SOCKET_MEM_NAME_BUF_LEN] = {0};
  uint32_t client_ring_slot = 0;
  uint64_t client_ring_offset = 0;
  uint64_t client_ring_size = 0;

  if (session->wait_for_request(request_ddl_ns, client_mem_name,
                                &client_ring_slot, &client_ring_offset,
                                &client_ring_size)) {
    bool published = publish_server_mapping(client_mem_name, &local_ring_slot,
                                            client_ring_offset);
    result = session->finish_server_attach(published, ddl_ns);
    if (published && result != UB_SOCKET_REGISTER_SUCCESS) {
      UBSocketManager::detach_fd(_socket_fd);
    }
  }
  UBSocketSessionCaches::remove(&local_ep, &remote_ep);
  bool success = result == UB_SOCKET_REGISTER_SUCCESS;
  if (!success) { UBSocketRingSlots::release(local_ring_slot); }
  char peer[UB_SOCKET_PEER_TEXT_BUF_LEN];
  ub_socket_peer_to_string(_socket_fd, peer, sizeof(peer));

  UB_LOG(UB_SOCKET, success ? UB_LOG_INFO : UB_LOG_WARNING,
         "fd=%d peer=%s attach server finished remote=%s %s\n",
         _socket_fd, peer,
         client_mem_name[0] == '\0' ? "<none>" : client_mem_name,
         success ? "bind success" :
             (result == UB_SOCKET_REGISTER_ABORT ? "abort" : "fallback to TCP"));
  return result;
}

bool UBSocketAttach::publish_server_mapping(const char* client_mem_name,
                                            uint32_t* local_ring_slot,
                                            uint64_t client_ring_offset) {
  UBSocketMemMapping* remote_mapping = UBSocketMemMapping::acquire(client_mem_name, _mem_size);
  if (remote_mapping == NULL) { return false; }

  UBSocketConnection* conn = new UBSocketConnection(_socket_fd, remote_mapping,
                                                   *local_ring_slot,
                                                   client_ring_offset);
  *local_ring_slot = UB_SOCKET_RING_INVALID_SLOT;
  if (!UBSocketConnectionTable::publish(_socket_fd, conn)) {
    delete conn;
    return false;
  }
  return true;
}

int32_t UBSocketAttach::attach_client_once(int control_fd, const UBSocketEndpoint& request_local_ep,
                                           const UBSocketEndpoint& request_remote_ep,
                                           uint32_t request_id, uint64_t ddl_ns,
                                           uint32_t* local_ring_slot,
                                           uint64_t local_ring_offset,
                                           uint64_t local_ring_size,
                                           char* remote_mem_name, bool* retry) {
  *retry = false;
  remote_mem_name[0] = '\0';

  UBSocketAttachFrame attach_req =
      ub_socket_attach_frame(UB_SOCKET_ATTACH_REQ, request_id, UB_SOCKET_OK_CODE,
                             &request_local_ep, &request_remote_ep,
                             _local_mem_name->as_C_string(),
                             *local_ring_slot, local_ring_offset, local_ring_size);
  if (!ub_socket_attach_send(control_fd, attach_req, ddl_ns)) {
    *retry = UBSocketIO::is_retryable_error(errno);
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "fd=%d send attach req failed: %s\n",
           _socket_fd, strerror(errno));
    return UB_SOCKET_REGISTER_FALLBACK;
  }

  UBSocketAttachFrame attach_rsp;
  if (!ub_socket_attach_recv(control_fd, &attach_rsp, UB_SOCKET_ATTACH_RSP, ddl_ns)) {
    *retry = UBSocketIO::is_retryable_error(errno);
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "fd=%d recv attach rsp failed: %s\n",
           _socket_fd, strerror(errno));
    return UB_SOCKET_REGISTER_FALLBACK;
  }
  if (attach_rsp.request_id != request_id ||
      !ub_socket_endpoint_equals(&attach_rsp.local_endpoint, &request_local_ep) ||
      !ub_socket_endpoint_equals(&attach_rsp.remote_endpoint, &request_remote_ep) ||
      attach_rsp.error_code != UB_SOCKET_OK_CODE) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "fd=%d invalid attach rsp error=%u\n",
           _socket_fd, attach_rsp.error_code);
    return UB_SOCKET_REGISTER_FALLBACK;
  }
  strncpy(remote_mem_name, attach_rsp.mem_name, UB_SOCKET_MEM_NAME_LEN);
  remote_mem_name[UB_SOCKET_MEM_NAME_LEN] = '\0';

  UBSocketMemMapping* remote_mapping = UBSocketMemMapping::acquire(remote_mem_name, _mem_size);
  bool bind_ok = false;
  if (remote_mapping != NULL) {
    UBSocketConnection* conn = new UBSocketConnection(_socket_fd, remote_mapping,
                                                     *local_ring_slot,
                                                     attach_rsp.ring_offset);
    *local_ring_slot = UB_SOCKET_RING_INVALID_SLOT;
    if (UBSocketConnectionTable::publish(_socket_fd, conn)) {
      bind_ok = true;
    } else {
      delete conn;
    }
  }

  UBSocketAttachFrame attach_commit =
      ub_socket_attach_frame(UB_SOCKET_ATTACH_COMMIT, request_id,
                             bind_ok ? UB_SOCKET_OK_CODE : UB_SOCKET_ERROR_CODE,
                             &request_local_ep, &request_remote_ep, "");
  if (!ub_socket_attach_send(control_fd, attach_commit, ddl_ns)) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "fd=%d send attach commit failed: %s\n",
           _socket_fd, strerror(errno));
    if (bind_ok) { UBSocketManager::detach_fd(_socket_fd); }
    return bind_ok ? UB_SOCKET_REGISTER_ABORT : UB_SOCKET_REGISTER_FALLBACK;
  }
  if (!bind_ok) { return UB_SOCKET_REGISTER_FALLBACK; }

  UBSocketAttachFrame attach_ack;
  if (!ub_socket_attach_recv(control_fd, &attach_ack, UB_SOCKET_ATTACH_ACK, ddl_ns)) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "fd=%d recv attach ack failed: %s\n",
           _socket_fd, strerror(errno));
    ub_socket_abort_committed_attach(_socket_fd, bind_ok);
    return UB_SOCKET_REGISTER_ABORT;
  }
  if (attach_ack.request_id != request_id ||
      !ub_socket_endpoint_equals(&attach_ack.local_endpoint, &request_local_ep) ||
      !ub_socket_endpoint_equals(&attach_ack.remote_endpoint, &request_remote_ep) ||
      attach_ack.error_code != UB_SOCKET_OK_CODE) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "fd=%d invalid attach ack error=%u bind=%d\n",
           _socket_fd, attach_ack.error_code, bind_ok ? 1 : 0);
    ub_socket_abort_committed_attach(_socket_fd, bind_ok);
    return UB_SOCKET_REGISTER_ABORT;
  }
  return UB_SOCKET_REGISTER_SUCCESS;
}

int32_t UBSocketAttach::attach_client() {
  UBSocketEndpoint local_ep;
  UBSocketEndpoint remote_ep;
  if (!ub_socket_endpoint_get(_socket_fd, &local_ep, &remote_ep)) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "fd=%d attach client collect endpoints failed: %s\n",
           _socket_fd, strerror(errno));
    return UB_SOCKET_REGISTER_FALLBACK;
  }

  UBSocketEndpoint peer_ctrl;
  if (!UBSocketEndpointMap::control_endpoint_for_data(&remote_ep, &peer_ctrl)) {
    peer_ctrl = remote_ep;
    peer_ctrl.port = (uint16_t)UBSocketPort;
  }
  struct sockaddr_storage peer_ctrl_addr;
  socklen_t peer_ctrl_addr_len = 0;
  if (!ub_socket_endpoint_to_addr(&peer_ctrl, &peer_ctrl_addr, &peer_ctrl_addr_len)) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "fd=%d build control endpoint failed: %s\n",
           _socket_fd, strerror(errno));
    return UB_SOCKET_REGISTER_FALLBACK;
  }

  uint64_t ddl_ns = (uint64_t)os::javaTimeNanos() +
                         (uint64_t)UB_ATTACH_TIMEOUT_MS * NANOSECS_PER_MILLISEC;
  const uint32_t request_id = (uint32_t)os::javaTimeNanos();
  uint64_t local_ring_offset = 0;
  uint64_t local_ring_size = 0;
  uint32_t local_ring_slot = UBSocketRingSlots::alloc(&local_ring_offset, &local_ring_size);
  if (local_ring_slot == UB_SOCKET_RING_INVALID_SLOT) {
    UB_PROFILE_COUNT(UB_PROF_RING_ATTACH_NO_SLOT, 0);
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "fd=%d attach client no local ring slot\n", _socket_fd);
    return UB_SOCKET_REGISTER_FALLBACK;
  }

  char remote_mem_name[UB_SOCKET_MEM_NAME_BUF_LEN] = {0};
  int32_t result = UB_SOCKET_REGISTER_FALLBACK;
  while (result == UB_SOCKET_REGISTER_FALLBACK &&
         (uint64_t)os::javaTimeNanos() < ddl_ns) {
    int control_fd = UBSocketIO::connect(peer_ctrl.family, (const struct sockaddr*)&peer_ctrl_addr,
                                         peer_ctrl_addr_len, ddl_ns);
    if (control_fd < 0) {
      if (UBSocketIO::is_retryable_error(errno)) { continue; }
      UB_LOG(UB_SOCKET, UB_LOG_WARNING,
             "fd=%d connect control port=" UINTX_FORMAT " failed: %s\n",
             _socket_fd, UBSocketPort, strerror(errno));
      break;
    }

    bool retry = false;
    result = attach_client_once(control_fd, remote_ep, local_ep, request_id,
                                ddl_ns, &local_ring_slot, local_ring_offset,
                                local_ring_size, remote_mem_name, &retry);
    close(control_fd);
    if (retry) { continue; }
    if (result != UB_SOCKET_REGISTER_SUCCESS) { break; }
  }

  bool success = result == UB_SOCKET_REGISTER_SUCCESS;
  char peer[UB_SOCKET_PEER_TEXT_BUF_LEN];
  ub_socket_peer_to_string(_socket_fd, peer, sizeof(peer));
  UB_LOG(UB_SOCKET, success ? UB_LOG_INFO : UB_LOG_WARNING,
         "fd=%d peer=%s attach client finished remote=%s %s\n",
         _socket_fd, peer,
         remote_mem_name[0] == '\0' ? "<none>" : remote_mem_name,
         success ? "bind success" :
             (result == UB_SOCKET_REGISTER_ABORT ? "abort" : "fallback to TCP"));
  if (!success) { UBSocketRingSlots::release(local_ring_slot); }
  return result;
}
