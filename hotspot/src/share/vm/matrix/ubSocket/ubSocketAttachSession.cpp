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

#include "matrix/ubSocket/ubSocket.hpp"
#include "matrix/ubSocket/ubSocketConfig.hpp"
#include "matrix/ubSocket/ubSocketFrame.hpp"
#include "matrix/ubSocket/ubSocketIO.hpp"
#include "matrix/ubSocket/ubSocketUtils.hpp"
#include "matrix/matrixLog.hpp"
#include "runtime/mutex.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/os.hpp"

UBSocketAttachSession::UBSocketAttachSession(const UBSocketEndpoint* local_ep,
                                             const UBSocketEndpoint* remote_ep,
                                             const char* local_mem_name,
                                             uint32_t local_ring_slot,
                                             uint32_t local_connection_generation,
                                             uint64_t local_ring_offset,
                                             uint64_t local_ring_size,
                                             const UBSocketProtocolInfo& protocol,
                                             const UBSocketConfig* config)
  : _monitor(new Monitor(Mutex::leaf, "UBSocketAttachSession_lock")),
    _local_endpoint(*local_ep),
    _remote_endpoint(*remote_ep),
    _request_id(0),
    _protocol(protocol),
    _local_ring_slot(local_ring_slot),
    _local_connection_generation(local_connection_generation),
    _local_ring_offset(local_ring_offset),
    _local_ring_size(local_ring_size),
    _phase(ATTACH_REQUESTED),
    _next(NULL),
    _closing(false),
    _active_count(0),
    _config(config) {
  strncpy(_local_mem_name, local_mem_name, UB_SOCKET_MEM_NAME_LEN);
  _local_mem_name[UB_SOCKET_MEM_NAME_LEN] = '\0';
  memset(&_client_request, 0, sizeof(_client_request));
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

bool UBSocketAttachSession::matches_request(const UBSocketAttachFrame* request) const {
  bool local_match = false;
  if (_config->has_mapping_for_data(&request->local_endpoint)) {
    local_match =
        _config->matches_local_data(&request->local_endpoint, &_local_endpoint);
  } else {
    local_match = ub_socket_endpoint_equals(&request->local_endpoint, &_local_endpoint);
  }
  return local_match &&
         ub_socket_endpoint_equals(&request->remote_endpoint, &_remote_endpoint);
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

bool UBSocketAttachSession::wait_for_request(
    uint64_t ddl_ns, UBSocketAttachFrame* client_request) {
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

  *client_request = _client_request;
  return true;
}

bool UBSocketAttachSession::accept_request(const UBSocketAttachFrame* request) {
  MonitorLockerEx locker(_monitor, Mutex::_no_safepoint_check_flag);
  if (_phase != ATTACH_REQUESTED) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING,
           "session local_port=%u remote_port=%u reject req phase=%d\n",
           _local_endpoint.port, _remote_endpoint.port, (int)_phase);
    return false;
  }
  if (_request_id != 0 && _request_id != request->request_id) {
    return false;
  }
  _request_id = request->request_id;
  _client_request = *request;
  _phase = ATTACH_PREPARED;
  UB_LOG(UB_SOCKET, UB_LOG_INFO, "session local_port=%u remote_port=%u req accepted\n",
         _local_endpoint.port, _remote_endpoint.port);
  locker.notify_all();
  return true;
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

bool UBSocketAttachSession::wait_for_commit(uint64_t ddl_ns,
                                            bool* peer_commit_failed) {
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
  *peer_commit_failed = _phase == ATTACH_COMMIT_FAILED;
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
  if (!matches_request(request)) {
    return false;
  }
  if (!ub_socket_attach_compatible(*request, _protocol, true)) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING,
           "control fd=%d reject incompatible protocol options\n", control_fd);
    UBSocketAttachFrame response =
        ub_socket_attach_frame(UB_SOCKET_ATTACH_RSP, request->request_id,
                               UB_SOCKET_ERROR_CODE,
                               &request->local_endpoint,
                               &request->remote_endpoint, "", _protocol);
    if (!ub_socket_attach_send(control_fd, response, ddl_ns)) {
      UB_LOG(UB_SOCKET, UB_LOG_WARNING,
             "control fd=%d send incompatible attach rsp failed: %s\n",
             control_fd, strerror(errno));
    }
    finish_control(false);
    return false;
  }
  if (_request_id != 0 && _request_id != request->request_id) {
    return false;
  }
  if (!accept_request(request)) {
    return false;
  }
  if (!wait_for_response(ddl_ns)) {
    finish_control(false);
    return false;
  }

  UBSocketAttachFrame response =
      ub_socket_attach_frame(UB_SOCKET_ATTACH_RSP, _request_id,
                             is_prepared() ? UB_SOCKET_OK_CODE : UB_SOCKET_ERROR_CODE,
                             &request->local_endpoint, &request->remote_endpoint,
                             is_prepared() ? _local_mem_name : "",
                             _protocol, _local_connection_generation,
                             _local_ring_slot, _local_ring_offset,
                             _local_ring_size);
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
      !ub_socket_endpoint_equals(&commit.local_endpoint, &request->local_endpoint) ||
      !ub_socket_endpoint_equals(&commit.remote_endpoint, &request->remote_endpoint) ||
      commit.connection_generation != _local_connection_generation ||
      !ub_socket_attach_compatible(commit, _protocol, false)) {
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
                             &request->local_endpoint, &request->remote_endpoint, "",
                             _protocol, request->connection_generation);
  bool send_ok = ub_socket_attach_send(control_fd, ack, ddl_ns);
  if (!send_ok) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "control fd=%d send attach ack failed: %s\n",
           control_fd, strerror(errno));
  }
  finish_control(send_ok);
  return send_ok;
}

int32_t UBSocketAttachSession::finish_server_attach(bool prepare_success,
                                                    uint64_t ddl_ns) {
  publish_response(prepare_success);
  if (!prepare_success) {
    finish_pending(false, ddl_ns);
    return UB_SOCKET_REGISTER_FALLBACK;
  }

  bool peer_commit_failed = false;
  if (wait_for_commit(ddl_ns, &peer_commit_failed)) {
    return finish_pending(true, ddl_ns)
        ? UB_SOCKET_REGISTER_SUCCESS : UB_SOCKET_REGISTER_ABORT;
  }

  finish_pending(false, ddl_ns);
  if (peer_commit_failed) {
    return UB_SOCKET_REGISTER_FALLBACK;
  }
  return UB_SOCKET_REGISTER_ABORT;
}
