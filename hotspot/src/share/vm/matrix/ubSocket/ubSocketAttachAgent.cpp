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
static const int UB_SOCKET_CONTROL_BUSY_POLL_MS = 10;
static const int UB_SOCKET_CONTROL_IDLE_POLL_MS = 100;

class UBSocketAttachAgentState : public CHeapObj<mtInternal> {
 public:
  Monitor* lock;
  bool started;
  volatile bool should_stop;
  bool exited;
  int listen_fd;

  UBSocketAttachAgentState()
    : lock(new Monitor(Mutex::leaf, "UBSocketAttachAgent_lock")),
      started(false),
      should_stop(false),
      exited(true),
      listen_fd(-1) {}
};

static UBSocketAttachAgentState* g_attach_agent_state = NULL;

static UBSocketAttachAgentState* attach_agent_state() {
  if (g_attach_agent_state == NULL) {
    g_attach_agent_state = new UBSocketAttachAgentState();
  }
  return g_attach_agent_state;
}

static int create_listener_socket(uint16_t port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) { return -1; }

  int reuse_addr = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);

  if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0 ||
      listen(fd, UB_SOCKET_CONTROL_LISTEN_BACKLOG) != 0) {
    int bind_errno = errno;
    close(fd);
    errno = bind_errno;
    return -1;
  }
  return fd;
}

bool UBSocketAttachAgent::handle_control_connection(int control_fd, bool* keep_open) {
  uint64_t ddl_ns = (uint64_t)os::javaTimeNanos() +
                         (uint64_t)UB_SOCKET_ATTACH_TIMEOUT_MS * NANOSECS_PER_MILLISEC;
  UBSocketControlFrame request;
  *keep_open = false;
  if (!ub_frame_recv(control_fd, &request, UB_SOCKET_CONTROL_ATTACH_REQ, ddl_ns)) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "control fd=%d recv attach req failed: %s\n",
           control_fd, strerror(errno));
    return false;
  }

  UBSocketAttachSession* session =
      UBSocketAttachSessionTable::find(&request.local_endpoint, &request.remote_endpoint);
  if (session != NULL) {
    return session->drive_server_handshake(control_fd, &request, ddl_ns);
  }
  // The data fd may not have registered its session yet. Keep the
  // control socket open and cache the request so the listener can retry after
  // the server-side attach path publishes the session entry.
  if (!UBSocketEarlyRequestQueue::cache(control_fd, &request, ddl_ns)) { return false; }
  *keep_open = true;
  return true;
}

void UBSocketAttachAgent::listener_entry(JavaThread* thread, TRAPS) {
  UBSocketAttachAgentState* state = attach_agent_state();
  UB_LOG(UB_SOCKET, UB_LOG_DEBUG, "attach agent started port=%d\n", UBSocketControlPort);
  while (!state->should_stop) {
    // Cached early requests are retried before blocking in accept() so that
    // short accept/publish races do not force the client side to reconnect.
    int queued = UBSocketEarlyRequestQueue::count();
    while (queued-- > 0) {
      int cached_fd = -1;
      uint64_t cached_ddl_ns = 0;
      UBSocketControlFrame cached_request;
      if (!UBSocketEarlyRequestQueue::take_one(&cached_fd, &cached_request, &cached_ddl_ns)) {
        break;
      }

      bool expired = (uint64_t)os::javaTimeNanos() >= cached_ddl_ns;
      UBSocketAttachSession* session =
          UBSocketAttachSessionTable::find(&cached_request.local_endpoint,
                                           &cached_request.remote_endpoint);
      if (!expired && session == NULL) {
        UBSocketEarlyRequestQueue::cache(cached_fd, &cached_request, cached_ddl_ns);
        continue;
      }
      if (session != NULL) {
        session->drive_server_handshake(cached_fd, &cached_request, cached_ddl_ns);
      }
      close(cached_fd);
    }

    int listen_fd = state->listen_fd;
    if (listen_fd < 0) { break; }

    int poll_timeout_ms = UBSocketEarlyRequestQueue::has_requests() ? UB_SOCKET_CONTROL_BUSY_POLL_MS
                                                                    : UB_SOCKET_CONTROL_IDLE_POLL_MS;
    uint64_t poll_ddl_ns = (uint64_t)os::javaTimeNanos() +
                                (uint64_t)poll_timeout_ms * NANOSECS_PER_MILLISEC;
    if (!UBSocketIO::wait_fd(listen_fd, POLLIN, poll_ddl_ns)) { continue; }

    int control_fd = UBSocketIO::accept(listen_fd);
    if (control_fd < 0) { continue; }
    bool keep_open = false;
    handle_control_connection(control_fd, &keep_open);
    if (!keep_open) { close(control_fd); }
  }

  state->lock->lock();
  state->exited = true;
  state->lock->notify_all();
  state->lock->unlock();
}

bool UBSocketAttachAgent::start() {
  UBSocketAttachAgentState* state = attach_agent_state();
  int control_port = (int)UBSocketControlPort;

  state->lock->lock();
  if (state->started) {
    state->lock->unlock();
    return true;
  }

  int listen_fd = create_listener_socket((uint16_t)control_port);
  if (listen_fd < 0) {
    state->lock->unlock();
    return false;
  }

  state->should_stop = false;
  state->exited = false;
  state->listen_fd = listen_fd;

  JavaThread* thread = UBSocketThreadUtils::start_daemon(&listener_entry, "Attach Agent");
  if (thread == NULL) {
    close(listen_fd);
    state->listen_fd = -1;
    state->exited = true;
    state->lock->unlock();
    errno = ENOMEM;
    return false;
  }

  state->started = true;
  state->lock->unlock();
  return true;
}

void UBSocketAttachAgent::shutdown() {
  if (g_attach_agent_state == NULL) { return; }
  UBSocketAttachAgentState* state = g_attach_agent_state;

  state->lock->lock();
  bool started = state->started;
  int listen_fd = state->listen_fd;
  state->should_stop = true;
  state->started = false;
  state->listen_fd = -1;
  state->lock->notify_all();
  state->lock->unlock();

  if (listen_fd >= 0) { close(listen_fd); }

  state->lock->lock();
  while (started && !state->exited) { state->lock->wait(false); }
  state->lock->unlock();

  UBSocketEarlyRequestQueue::cleanup();
  UBSocketAttachSessionTable::cleanup();
}
