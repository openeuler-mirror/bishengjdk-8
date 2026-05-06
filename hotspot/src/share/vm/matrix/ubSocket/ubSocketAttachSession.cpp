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

#include "matrix/ubSocket/ubSocketAttachSession.hpp"

#include <string.h>

#include "matrix/ubSocket/ubSocketFrame.hpp"
#include "matrix/ubSocket/ubSocketIO.hpp"
#include "matrix/matrixLog.hpp"
#include "runtime/mutex.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/os.hpp"

UBSocketAttachSession::UBSocketAttachSession(const UBSocketEndpoint* local_ep,
                                             const UBSocketEndpoint* remote_ep,
                                             const char* local_mem_name)
  : _monitor(new Monitor(Mutex::leaf, "UBSocketAttachSession_lock")),
    _local_endpoint(*local_ep),
    _remote_endpoint(*remote_ep),
    _request_id(0),
    _phase(ATTACH_REQUESTED),
    _next(NULL),
    _closing(false),
    _active_count(0) {
  strncpy(_local_mem_name, local_mem_name, UB_SOCKET_MEM_NAME_LEN);
  _local_mem_name[UB_SOCKET_MEM_NAME_LEN] = '\0';
  _client_mem_name[0] = '\0';
}

UBSocketAttachSession::~UBSocketAttachSession() {
  delete _monitor;
}

bool UBSocketAttachSession::wait_until(uint64_t ddl_ns) {
  uint64_t now_ns = (uint64_t)os::javaTimeNanos();
  if (now_ns >= ddl_ns) {
    errno = ETIMEDOUT;
    return false;
  }

  const uint64_t delta_ns = ddl_ns - now_ns;
  long timeout_ms =
      (long)((delta_ns + NANOSECS_PER_MILLISEC - 1) / NANOSECS_PER_MILLISEC);
  if (timeout_ms <= 0) { timeout_ms = 1; }

  if (_monitor->wait(false, timeout_ms)) {
    errno = ETIMEDOUT;
    return false;
  }
  return true;
}

bool UBSocketAttachSession::matches(const UBSocketEndpoint* local_ep,
                                    const UBSocketEndpoint* remote_ep) const {
  return ub_socket_endpoint_equals(&_local_endpoint, local_ep) &&
         ub_socket_endpoint_equals(&_remote_endpoint, remote_ep);
}

bool UBSocketAttachSession::try_pin() {
  MonitorLockerEx locker(_monitor, Mutex::_no_safepoint_check_flag);
  if (_closing) { return false; }
  _active_count++;
  return true;
}

void UBSocketAttachSession::unpin() {
  MonitorLockerEx locker(_monitor, Mutex::_no_safepoint_check_flag);
  _active_count--;
  if (_closing && _active_count == 0) {
    locker.notify_all();
  }
}

void UBSocketAttachSession::close_and_wait() {
  MonitorLockerEx locker(_monitor, Mutex::_no_safepoint_check_flag);
  _closing = true;
  if (!done()) {
    _phase = ATTACH_ABORTED;
  }
  locker.notify_all();
  while (_active_count > 0) {
    locker.wait(Mutex::_no_safepoint_check_flag);
  }
}

bool UBSocketAttachSession::wait_for_request(uint64_t ddl_ns, char* client_mem_name) {
  MonitorLockerEx locker(_monitor, Mutex::_no_safepoint_check_flag);
  while (_phase == ATTACH_REQUESTED) {
    if (!wait_until(ddl_ns)) {
      UB_LOG(UB_SOCKET, UB_LOG_WARNING, "session local_port=%u remote_port=%u wait req timed out\n",
             _local_endpoint.port, _remote_endpoint.port);
      _phase = ATTACH_ABORTED;
      break;
    }
  }
  if (failed()) { return false; }

  strncpy(client_mem_name, _client_mem_name, UB_SOCKET_MEM_NAME_LEN);
  client_mem_name[UB_SOCKET_MEM_NAME_LEN] = '\0';
  return true;
}

void UBSocketAttachSession::accept_request(const char* client_mem_name, uint32_t request_id) {
  MonitorLockerEx locker(_monitor, Mutex::_no_safepoint_check_flag);
  _request_id = request_id;
  strncpy(_client_mem_name, client_mem_name, UB_SOCKET_MEM_NAME_LEN);
  _client_mem_name[UB_SOCKET_MEM_NAME_LEN] = '\0';
  _phase = ATTACH_PREPARED;
  UB_LOG(UB_SOCKET, UB_LOG_INFO, "session local_port=%u remote_port=%u req accepted\n",
         _local_endpoint.port, _remote_endpoint.port);
  locker.notify_all();
}

void UBSocketAttachSession::publish_response(bool success) {
  MonitorLockerEx locker(_monitor, Mutex::_no_safepoint_check_flag);
  _phase = success ? ATTACH_COMMITTED : ATTACH_PREPARE_FAILED;
  UB_LOG(UB_SOCKET, UB_LOG_INFO, "session local_port=%u remote_port=%u rsp prepared=%d\n",
         _local_endpoint.port, _remote_endpoint.port, success ? 1 : 0);
  locker.notify_all();
}

bool UBSocketAttachSession::wait_for_response(uint64_t ddl_ns) {
  MonitorLockerEx locker(_monitor, Mutex::_no_safepoint_check_flag);
  while (_phase == ATTACH_PREPARED) {
    if (!wait_until(ddl_ns)) {
      UB_LOG(UB_SOCKET, UB_LOG_WARNING, "session local_port=%u remote_port=%u wait rsp timed out\n",
             _local_endpoint.port, _remote_endpoint.port);
      _phase = ATTACH_ABORTED;
      break;
    }
  }
  return !failed();
}

bool UBSocketAttachSession::wait_for_commit(uint64_t ddl_ns) {
  MonitorLockerEx locker(_monitor, Mutex::_no_safepoint_check_flag);
  while (_phase == ATTACH_COMMITTED) {
    if (!wait_until(ddl_ns)) {
      UB_LOG(UB_SOCKET, UB_LOG_WARNING,
             "session local_port=%u remote_port=%u wait commit timed out\n",
             _local_endpoint.port, _remote_endpoint.port);
      _phase = ATTACH_ABORTED;
      break;
    }
  }
  return !failed() && _phase == ATTACH_FINALIZED;
}

void UBSocketAttachSession::accept_commit(bool success) {
  MonitorLockerEx locker(_monitor, Mutex::_no_safepoint_check_flag);
  _phase = success ? ATTACH_FINALIZED : ATTACH_COMMIT_FAILED;
  UB_LOG(UB_SOCKET, UB_LOG_INFO, "session local_port=%u remote_port=%u commit accepted=%d\n",
         _local_endpoint.port, _remote_endpoint.port, success ? 1 : 0);
  locker.notify_all();
}

bool UBSocketAttachSession::wait_for_final_result(uint64_t ddl_ns) {
  MonitorLockerEx locker(_monitor, Mutex::_no_safepoint_check_flag);
  while (_phase == ATTACH_FINALIZED || _phase == ATTACH_COMMIT_FAILED) {
    if (!wait_until(ddl_ns)) {
      UB_LOG(UB_SOCKET, UB_LOG_WARNING,
             "session local_port=%u remote_port=%u wait final timed out\n",
             _local_endpoint.port, _remote_endpoint.port);
      _phase = ATTACH_ABORTED;
      break;
    }
  }
  return !failed();
}

bool UBSocketAttachSession::finish_pending(bool success, uint64_t ddl_ns) {
  MonitorLockerEx locker(_monitor, Mutex::_no_safepoint_check_flag);
  if (done() || failed()) {
    return !failed();
  }

  _phase = success ? ATTACH_PENDING_SUCCESS : ATTACH_PENDING_FAILURE;
  UB_LOG(UB_SOCKET, UB_LOG_INFO, "session local_port=%u remote_port=%u pending=%d\n",
         _local_endpoint.port, _remote_endpoint.port, success ? 1 : 0);
  locker.notify_all();
  while (_phase == ATTACH_PENDING_SUCCESS || _phase == ATTACH_PENDING_FAILURE) {
    if (!wait_until(ddl_ns)) {
      UB_LOG(UB_SOCKET, UB_LOG_WARNING,
             "session local_port=%u remote_port=%u wait control done timed out\n",
             _local_endpoint.port, _remote_endpoint.port);
      _phase = ATTACH_ABORTED;
      break;
    }
  }
  return !failed();
}

void UBSocketAttachSession::finish_control(bool success) {
  MonitorLockerEx locker(_monitor, Mutex::_no_safepoint_check_flag);
  _phase = success ? ATTACH_DONE : ATTACH_ABORTED;
  UB_LOG(UB_SOCKET, UB_LOG_INFO, "session local_port=%u remote_port=%u control done=%d\n",
         _local_endpoint.port, _remote_endpoint.port, success ? 1 : 0);
  locker.notify_all();
}

bool UBSocketAttachSession::drive_server_handshake(int control_fd,
                                                   const UBSocketAttachFrame* request,
                                                   uint64_t ddl_ns) {
  if (!ub_socket_endpoint_equals(&request->local_endpoint, &_local_endpoint) ||
      !ub_socket_endpoint_equals(&request->remote_endpoint, &_remote_endpoint)) {
    return false;
  }
  if (_request_id != 0 && _request_id != request->request_id) {
    return false;
  }
  accept_request(request->mem_name, request->request_id);
  if (!wait_for_response(ddl_ns)) {
    finish_control(false);
    return false;
  }

  UBSocketAttachFrame response =
      ub_socket_attach_frame(UB_SOCKET_ATTACH_RSP, _request_id,
                             is_prepared() ? UB_SOCKET_OK_CODE : UB_SOCKET_ERROR_CODE,
                             &_local_endpoint, &_remote_endpoint,
                             is_prepared() ? _local_mem_name : "");
  if (!ub_socket_attach_send(control_fd, response, ddl_ns)) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "control fd=%d send attach rsp failed: %s\n",
           control_fd, strerror(errno));
    finish_control(false);
    return false;
  }
  if (response.error_code != UB_SOCKET_OK_CODE) {
    finish_control(true);
    return false;
  }

  UBSocketAttachFrame commit;
  if (!ub_socket_attach_recv(control_fd, &commit, UB_SOCKET_ATTACH_COMMIT, ddl_ns)) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "control fd=%d recv attach commit failed: %s\n",
           control_fd, strerror(errno));
    finish_control(false);
    return false;
  }
  if (commit.request_id != _request_id ||
      !ub_socket_endpoint_equals(&commit.local_endpoint, &_local_endpoint) ||
      !ub_socket_endpoint_equals(&commit.remote_endpoint, &_remote_endpoint)) {
    finish_control(false);
    return false;
  }

  accept_commit(commit.error_code == UB_SOCKET_OK_CODE);
  if (!wait_for_final_result(ddl_ns)) {
    finish_control(false);
    return false;
  }

  UBSocketAttachFrame ack =
      ub_socket_attach_frame(UB_SOCKET_ATTACH_ACK, _request_id,
                             is_finalized_ok() ? UB_SOCKET_OK_CODE : UB_SOCKET_ERROR_CODE,
                             &_local_endpoint, &_remote_endpoint, "");
  bool send_ok = ub_socket_attach_send(control_fd, ack, ddl_ns);
  if (!send_ok) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "control fd=%d send attach ack failed: %s\n",
           control_fd, strerror(errno));
  }
  finish_control(send_ok);
  return send_ok;
}

bool UBSocketAttachSession::finish_server_attach(bool prepare_success, uint64_t ddl_ns) {
  publish_response(prepare_success);
  if (prepare_success && wait_for_commit(ddl_ns)) {
    return finish_pending(true, ddl_ns);
  }
  finish_pending(false, ddl_ns);
  return false;
}
