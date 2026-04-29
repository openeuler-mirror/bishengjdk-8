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

#include "matrix/ubSocket/ubSocketAttach.hpp"

#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "matrix/matrixLog.hpp"
#include "matrix/ubSocket/ubSocketAttachSession.hpp"
#include "matrix/ubSocket/ubSocketFrame.hpp"
#include "matrix/ubSocket/ubSocketIO.hpp"
#include "matrix/ubSocket/ubSocketUtils.hpp"
#include "runtime/interfaceSupport.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/thread.hpp"

// Max number of pending attach connections in listen(fd, backlog)
static const int UB_SOCKET_CONTROL_LISTEN_BACKLOG = 128;

Monitor* UBSocketAttachAgent::_agent_lock = NULL;
bool UBSocketAttachAgent::_started = false;
volatile bool UBSocketAttachAgent::_should_stop = false;
bool UBSocketAttachAgent::_exited = true;
int UBSocketAttachAgent::_listen_fd = -1;
bool UBSocketAttachAgent::_dual_stack = false;

void UBSocketAttachAgent::init() {
  _agent_lock = new Monitor(Mutex::leaf, "UBSocketAttachAgent_lock");
  _started = false;
  _should_stop = false;
  _exited = true;
  _listen_fd = -1;
  _dual_stack = false;
}

static int create_listener_socket(uint16_t port, bool* dual_stack_enabled) {
  if (dual_stack_enabled != NULL) { *dual_stack_enabled = false; }

  int fd = socket(AF_INET6, SOCK_STREAM, 0);
  if (fd >= 0) {
    int reuse_addr = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr));
    int dual_stack = 0;
    if (setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &dual_stack, sizeof(dual_stack)) == 0) {
      struct sockaddr_in6 addr6;
      memset(&addr6, 0, sizeof(addr6));
      addr6.sin6_family = AF_INET6;
      addr6.sin6_addr = in6addr_any;
      addr6.sin6_port = htons(port);

      if (bind(fd, (struct sockaddr*)&addr6, sizeof(addr6)) == 0 &&
          listen(fd, UB_SOCKET_CONTROL_LISTEN_BACKLOG) == 0) {
        if (dual_stack_enabled != NULL) { *dual_stack_enabled = true; }
        return fd;
      }
    }
    close(fd);
  }

  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) { return -1; }

  int reuse_addr = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr));

  struct sockaddr_in addr4;
  memset(&addr4, 0, sizeof(addr4));
  addr4.sin_family = AF_INET;
  addr4.sin_addr.s_addr = htonl(INADDR_ANY);
  addr4.sin_port = htons(port);

  if (bind(fd, (struct sockaddr*)&addr4, sizeof(addr4)) != 0 ||
      listen(fd, UB_SOCKET_CONTROL_LISTEN_BACKLOG) != 0) {
    close(fd);
    return -1;
  }
  return fd;
}

bool UBSocketAttachAgent::handle_control_connection(int control_fd, bool* keep_open) {
  uint64_t ddl_ns = (uint64_t)os::javaTimeNanos() +
                         (uint64_t)UB_ATTACH_TIMEOUT_MS * NANOSECS_PER_MILLISEC;
  UBSocketAttachFrame request;
  *keep_open = false;
  if (!ub_socket_attach_recv(control_fd, &request, UB_SOCKET_ATTACH_REQ, ddl_ns)) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "control fd=%d recv attach req failed: %s\n",
           control_fd, strerror(errno));
    return false;
  }

  UBSocketAttachSession* session =
      UBSocketSessionCaches::find(&request.local_endpoint, &request.remote_endpoint);
  if (session != NULL) {
    bool result = session->drive_server_handshake(control_fd, &request, ddl_ns);
    UBSocketSessionCaches::release(session);
    return result;
  }
  // The data fd may not have registered its session yet. Keep the
  // control socket open and cache the request so the listener can retry after
  // the server-side attach path publishes the session entry.
  if (!UBSocketEarlyReqQueue::cache(control_fd, &request, ddl_ns)) { return false; }
  *keep_open = true;
  return true;
}

void UBSocketAttachAgent::listener_entry(JavaThread* thread, TRAPS) {
  if (_dual_stack) {
    UB_LOG(UB_SOCKET, UB_LOG_INFO,
           "attach agent started bind=::(in6addr_any) dual_stack port=%d\n",
           UBSocketPort);
  } else {
    UB_LOG(UB_SOCKET, UB_LOG_INFO,
           "attach agent started bind=0.0.0.0(INADDR_ANY) ipv4_only port=%d\n",
           UBSocketPort);
  }
  while (!_should_stop) {
    // Cached early requests are retried before blocking in accept() so that
    // short accept/publish races do not force the client side to reconnect.
    int queued = UBSocketEarlyReqQueue::count();
    while (queued-- > 0) {
      int cached_fd = -1;
      uint64_t cached_ddl_ns = 0;
      UBSocketAttachFrame cached_request;
      if (!UBSocketEarlyReqQueue::take_one(&cached_fd, &cached_request, &cached_ddl_ns)) {
        break;
      }

      bool expired = (uint64_t)os::javaTimeNanos() >= cached_ddl_ns;
      UBSocketAttachSession* session =
          UBSocketSessionCaches::find(&cached_request.local_endpoint,
                                      &cached_request.remote_endpoint);
      if (!expired && session == NULL) {
        UBSocketEarlyReqQueue::cache(cached_fd, &cached_request, cached_ddl_ns);
        continue;
      }
      if (session != NULL) {
        session->drive_server_handshake(cached_fd, &cached_request, cached_ddl_ns);
        UBSocketSessionCaches::release(session);
      }
      close(cached_fd);
    }

    int listen_fd = _listen_fd;
    if (listen_fd < 0) { break; }

    int poll_timeout_ms = UBSocketEarlyReqQueue::has_requests()
                          ? UB_ATTACH_IO_BUSY_POLL_MS
                          : UB_ATTACH_IO_BUSY_POLL_MS * 10;
    uint64_t poll_ddl_ns = (uint64_t)os::javaTimeNanos() +
                                (uint64_t)poll_timeout_ms * NANOSECS_PER_MILLISEC;
    if (!UBSocketIO::wait_fd(listen_fd, POLLIN, poll_ddl_ns)) { continue; }

    int control_fd = UBSocketIO::accept(listen_fd);
    if (control_fd < 0) { continue; }
    bool keep_open = false;
    handle_control_connection(control_fd, &keep_open);
    if (!keep_open) { close(control_fd); }
  }

  {
    MonitorLockerEx locker(_agent_lock, Mutex::_no_safepoint_check_flag);
    _exited = true;
    locker.notify_all();
  }
}

bool UBSocketAttachAgent::start() {
  MonitorLockerEx locker(_agent_lock, Mutex::_no_safepoint_check_flag);

  if (_started) { return true; }
  bool dual_stack = false;
  int listen_fd = create_listener_socket((uint16_t)UBSocketPort, &dual_stack);
  if (listen_fd < 0) { return false; }

  _should_stop = false;
  _exited = false;
  _listen_fd = listen_fd;
  _dual_stack = dual_stack;

  JavaThread* thread = UBSocketThreadUtils::start_daemon(&listener_entry, "Attach Agent");
  if (thread == NULL) {
    close(listen_fd);
    _listen_fd = -1;
    _exited = true;
    errno = ENOMEM;
    return false;
  }

  _started = true;
  return true;
}

void UBSocketAttachAgent::shutdown() {
  bool started = false;
  int listen_fd = -1;
  {
    MonitorLockerEx locker(_agent_lock, Mutex::_no_safepoint_check_flag);
    started = _started;
    listen_fd = _listen_fd;
    _should_stop = true;
    _started = false;
    _listen_fd = -1;
    _dual_stack = false;
    locker.notify_all();
  }

  if (listen_fd >= 0) { close(listen_fd); }

  {
    MonitorLockerEx locker(_agent_lock, Mutex::_no_safepoint_check_flag);
    while (started && !_exited) { locker.wait(Mutex::_no_safepoint_check_flag); }
  }

  UBSocketEarlyReqQueue::cleanup();
  UBSocketSessionCaches::cleanup();
}
