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
#include "matrix/ubSocket/ubSocketFrame.hpp"
#include "matrix/ubSocket/ubSocketIO.hpp"
#include "matrix/ubSocket/ubSocketMemMapping.hpp"
#include "matrix/ubSocket/ubSocketUtils.hpp"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "matrix/matrixLog.hpp"

UBSocketAttach::UBSocketAttach(int fd, bool is_server, Symbol* local_mem_name, size_t mem_size)
  : _socket_fd(fd),
    _is_server(is_server),
    _local_mem_name(local_mem_name),
    _mem_size(mem_size) {
  guarantee(_local_mem_name != NULL, "must be");
}

bool UBSocketAttach::do_attach() {
  return _is_server ? attach_server() : attach_client();
}

bool UBSocketAttach::attach_server() {
  if (!UBSocketAttachAgent::start()) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "fd=%d attach server start agent failed: %s\n",
           _socket_fd, strerror(errno));
    return false;
  }

  UBSocketEndpoint local_ep;
  UBSocketEndpoint remote_ep;
  if (!ub_frame_get_endpoints(_socket_fd, &local_ep, &remote_ep)) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "fd=%d attach server collect endpoints failed: %s\n",
           _socket_fd, strerror(errno));
    return false;
  }

  ResourceMark rm;
  UBSocketAttachSession* session =
      new (std::nothrow) UBSocketAttachSession(&local_ep, &remote_ep, _local_mem_name->as_C_string());
  if (session == NULL || !UBSocketAttachSessionTable::add(session)) {
    delete session;
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "fd=%d attach server publish session failed: %s\n",
           _socket_fd, strerror(errno));
    return false;
  }

  uint64_t ddl_ns = (uint64_t)os::javaTimeNanos() +
                         (uint64_t)UB_SOCKET_ATTACH_TIMEOUT_MS * NANOSECS_PER_MILLISEC;
  bool success = false;
  char client_mem_name[UB_SOCKET_MEM_NAME_BUF_LEN] = {0};

  if (session->wait_for_request(ddl_ns, client_mem_name)) {
    UBSocketMemMapping* remote_mapping =
        UBSocketMemMapping::acquire(SymbolTable::new_symbol(client_mem_name, JavaThread::current()),
                                    _mem_size);
    bool prepare_success = remote_mapping != NULL;
    if (prepare_success && session->drive_client_handshake(true, ddl_ns)) {
      UBSocketInfoList* info_list = new UBSocketInfoList(_socket_fd);
      info_list->set_mem_addr(remote_mapping->addr());
      info_list->set_mem_name(remote_mapping->name());
      info_list->set_mem_mapping(remote_mapping);
      SocketDataInfoTable::instance()->publish(_socket_fd, info_list);
      if (UBSocketManager::package_timeout > 0) {
        UnreadMsgTable::instance()->register_fd(_socket_fd);
      }
      success = true;
      remote_mapping = NULL;
    }
    if (remote_mapping != NULL) {
      remote_mapping->release(NULL);
    }
    if (!prepare_success) {
      session->drive_client_handshake(false, ddl_ns);
    }
  }
  UBSocketAttachSessionTable::remove(&local_ep, &remote_ep);

  UB_LOG(UB_SOCKET, success ? UB_LOG_DEBUG : UB_LOG_WARNING,
         "fd=%d attach server finished remote=%s %s\n",
         _socket_fd, client_mem_name[0] == '\0' ? "<none>" : client_mem_name,
         success ? "bind success" : "fallback to TCP");
  return success;
}

bool UBSocketAttach::attach_client_once(int control_fd, const UBSocketEndpoint& request_local_ep,
                                        const UBSocketEndpoint& request_remote_ep,
                                        uint64_t ddl_ns, char* remote_mem_name, bool* retry) {
  *retry = false;
  remote_mem_name[0] = '\0';

  UBSocketControlFrame attach_req;
  ub_frame_init(&attach_req, UB_SOCKET_CONTROL_ATTACH_REQ);
  attach_req.local_endpoint = request_local_ep;
  attach_req.remote_endpoint = request_remote_ep;
  _local_mem_name->as_C_string(attach_req.mem_name, (int)sizeof(attach_req.mem_name));
  if (!ub_frame_send(control_fd, &attach_req, ddl_ns)) {
    *retry = UBSocketIO::is_retryable_error(errno);
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "fd=%d send attach req failed: %s\n",
           _socket_fd, strerror(errno));
    return false;
  }

  UBSocketControlFrame attach_rsp;
  if (!ub_frame_recv(control_fd, &attach_rsp, UB_SOCKET_CONTROL_ATTACH_RSP, ddl_ns)) {
    *retry = UBSocketIO::is_retryable_error(errno);
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "fd=%d recv attach rsp failed: %s\n",
           _socket_fd, strerror(errno));
    return false;
  }
  if (attach_rsp.success == 0 ||
      strnlen(attach_rsp.mem_name, UB_SOCKET_MEM_NAME_BUF_LEN) >= UB_SOCKET_MEM_NAME_BUF_LEN) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "fd=%d invalid attach rsp success=%u\n",
           _socket_fd, attach_rsp.success);
    return false;
  }
  strncpy(remote_mem_name, attach_rsp.mem_name, UB_SOCKET_MEM_NAME_LEN);
  remote_mem_name[UB_SOCKET_MEM_NAME_LEN] = '\0';

  UBSocketMemMapping* remote_mapping =
      UBSocketMemMapping::acquire(SymbolTable::new_symbol(remote_mem_name, JavaThread::current()),
                                  _mem_size);
  bool bind_ok = false;
  if (remote_mapping != NULL) {
    UBSocketInfoList* info_list = new UBSocketInfoList(_socket_fd);
    info_list->set_mem_addr(remote_mapping->addr());
    info_list->set_mem_name(remote_mapping->name());
    info_list->set_mem_mapping(remote_mapping);
    SocketDataInfoTable::instance()->publish(_socket_fd, info_list);
    if (UBSocketManager::package_timeout > 0) {
      UnreadMsgTable::instance()->register_fd(_socket_fd);
    }
    bind_ok = true;
    remote_mapping = NULL;
  } else {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "fd=%d attach client map remote=%s failed\n",
           _socket_fd, remote_mem_name);
  }

  UBSocketControlFrame attach_commit;
  ub_frame_init(&attach_commit, UB_SOCKET_CONTROL_ATTACH_COMMIT);
  attach_commit.local_endpoint = request_local_ep;
  attach_commit.remote_endpoint = request_remote_ep;
  attach_commit.success = bind_ok ? 1 : 0;
  if (!ub_frame_send(control_fd, &attach_commit, ddl_ns)) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "fd=%d send attach commit failed: %s\n",
           _socket_fd, strerror(errno));
    if (bind_ok) { ub_socket_unbind_remote_mapping(_socket_fd, NULL, 0, NULL); }
    return false;
  }

  UBSocketControlFrame attach_ack;
  if (!ub_frame_recv(control_fd, &attach_ack, UB_SOCKET_CONTROL_ATTACH_ACK, ddl_ns)) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "fd=%d recv attach ack failed: %s\n",
           _socket_fd, strerror(errno));
    if (bind_ok) { ub_socket_unbind_remote_mapping(_socket_fd, NULL, 0, NULL); }
    return false;
  }
  if (attach_ack.success == 0 || !bind_ok) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "fd=%d invalid attach ack success=%u bind=%d\n",
           _socket_fd, attach_ack.success, bind_ok ? 1 : 0);
    if (bind_ok) { ub_socket_unbind_remote_mapping(_socket_fd, NULL, 0, NULL); }
    return false;
  }
  return true;
}

bool UBSocketAttach::attach_client() {
  UBSocketEndpoint local_ep;
  UBSocketEndpoint remote_ep;
  if (!ub_frame_get_endpoints(_socket_fd, &local_ep, &remote_ep)) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "fd=%d attach client collect endpoints failed: %s\n",
           _socket_fd, strerror(errno));
    return false;
  }

  UBSocketEndpoint peer_ctrl = remote_ep;
  peer_ctrl.port = (uint16_t)UBSocketControlPort;
  struct sockaddr_storage peer_ctrl_addr;
  socklen_t peer_ctrl_addr_len = 0;
  if (!ub_frame_endpoint_to_addr(&peer_ctrl, &peer_ctrl_addr, &peer_ctrl_addr_len)) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "fd=%d build control endpoint failed: %s\n",
           _socket_fd, strerror(errno));
    return false;
  }

  uint64_t ddl_ns = (uint64_t)os::javaTimeNanos() +
                         (uint64_t)UB_SOCKET_ATTACH_TIMEOUT_MS * NANOSECS_PER_MILLISEC;

  char remote_mem_name[UB_SOCKET_MEM_NAME_BUF_LEN] = {0};
  bool success = false;
  while (!success && (uint64_t)os::javaTimeNanos() < ddl_ns) {
    int control_fd = UBSocketIO::connect(peer_ctrl.family, (const struct sockaddr*)&peer_ctrl_addr,
                                         peer_ctrl_addr_len, ddl_ns);
    if (control_fd < 0) {
      if (UBSocketIO::is_retryable_error(errno)) { continue; }
      UB_LOG(UB_SOCKET, UB_LOG_WARNING, "fd=%d connect control port=%ld failed: %s\n",
             _socket_fd, UBSocketControlPort, strerror(errno));
      break;
    }

    bool retry = false;
    success = attach_client_once(control_fd, remote_ep, local_ep, ddl_ns, remote_mem_name, &retry);
    close(control_fd);
    if (retry) { continue; }
    if (!success) { break; }
  }

  UB_LOG(UB_SOCKET, success ? UB_LOG_DEBUG : UB_LOG_WARNING,
         "fd=%d attach client finished remote=%s %s\n",
         _socket_fd, remote_mem_name[0] == '\0' ? "<none>" : remote_mem_name,
         success ? "bind success" : "fallback to TCP");
  return success;
}
