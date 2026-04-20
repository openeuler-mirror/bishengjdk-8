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

#include "matrix/ubSocket/ubSocketIO.hpp"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include "matrix/matrixLog.hpp"
#include "runtime/interfaceSupport.hpp"
#include "runtime/java.hpp"
#include "runtime/thread.hpp"

static const long UB_SOCKET_IO_POLL_MIN_MS = 1;
static const socklen_t UB_SOCKET_SO_ERROR_LEN = sizeof(int);

bool UBSocketIO::is_retryable_error(int error_code) {
  return error_code == EAGAIN || error_code == EINTR || error_code == ECONNRESET ||
         error_code == ECONNABORTED || error_code == EPIPE || error_code == ETIMEDOUT;
}

bool UBSocketIO::wait_fd(int fd, short events, uint64_t ddl_ns) {
  while (true) {
    uint64_t now_ns = os::javaTimeNanos();
    if (now_ns >= ddl_ns) {
      errno = ETIMEDOUT;
      return false;
    }

    long timeout_ms =
        (long)((ddl_ns - now_ns + NANOSECS_PER_MILLISEC - 1) / NANOSECS_PER_MILLISEC);
    if (timeout_ms <= 0) { timeout_ms = UB_SOCKET_IO_POLL_MIN_MS; }

    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = events;
    pfd.revents = 0;

    int poll_res = 0;
    {
      ThreadBlockInVM tbivm(JavaThread::current());
      poll_res = poll(&pfd, 1, (int)timeout_ms);
    }

    if (poll_res > 0) {
      if ((pfd.revents & POLLERR) != 0 || (pfd.revents & POLLHUP) != 0 ||
          (pfd.revents & POLLNVAL) != 0) {
        errno = ECONNRESET;
        return false;
      }
      if ((pfd.revents & events) != 0) {
        return true;
      }
    } else if (poll_res == 0) {
      errno = ETIMEDOUT;
      return false;
    } else if (errno != EINTR) {
      return false;
    }
  }
}

int UBSocketIO::connect(int family, const struct sockaddr* addr,
                        socklen_t addr_len, uint64_t ddl_ns) {
  int fd = socket(family, SOCK_STREAM, 0);
  if (fd < 0) { return -1; }

  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
    int set_errno = errno;
    close(fd);
    errno = set_errno;
    return -1;
  }

  int connect_res = 0;
  {
    ThreadBlockInVM tbivm(JavaThread::current());
    connect_res = ::connect(fd, addr, addr_len);
  }
  if (connect_res == 0) { return fd; }

  int connect_errno = errno;
  if (connect_errno == EINPROGRESS || connect_errno == EALREADY ||
      connect_errno == EWOULDBLOCK) {
    // Non-blocking connect() completion is still verified with SO_ERROR after
    // POLLOUT to distinguish a completed connect from a latent socket error.
    if (!wait_fd(fd, POLLOUT, ddl_ns)) {
      int wait_errno = errno;
      close(fd);
      errno = wait_errno;
      return -1;
    }

    int so_error = 0;
    socklen_t so_len = UB_SOCKET_SO_ERROR_LEN;
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &so_len) != 0) {
      int so_errno = errno;
      close(fd);
      errno = so_errno;
      return -1;
    }
    if (so_error == 0 || so_error == EISCONN) { return fd; }
    connect_errno = so_error;
  }

  close(fd);
  errno = connect_errno;
  return -1;
}

int UBSocketIO::accept(int listen_fd) {
  ThreadBlockInVM tbivm(JavaThread::current());
  return ::accept(listen_fd, NULL, NULL);
}

ssize_t UBSocketIO::read(int fd, void* buf, size_t len) {
  ThreadBlockInVM tbivm(JavaThread::current());
  ssize_t nread = ::read(fd, buf, len);
  UB_LOG(UB_SOCKET, UB_LOG_DEBUG, "Socket %d read %s res %ld\n", fd, buf, nread);
  return nread;
}

ssize_t UBSocketIO::write(int fd, const void* buf, size_t len) {
  ThreadBlockInVM tbivm(JavaThread::current());
  ssize_t nwrite = ::write(fd, buf, len);
  UB_LOG(UB_SOCKET, UB_LOG_DEBUG, "Socket %d write %s res %ld\n", fd, buf, nwrite);
  return nwrite;
}

ssize_t UBSocketIO::send(int fd, const void* buf, size_t len, int flags) {
  ThreadBlockInVM tbivm(JavaThread::current());
  return ::send(fd, buf, len, flags);
}

ssize_t UBSocketIO::recv(int fd, void* buf, size_t len, int flags) {
  ThreadBlockInVM tbivm(JavaThread::current());
  return ::recv(fd, buf, len, flags);
}

bool UBSocketIO::send_all(int fd, const void* buf, size_t len,
                          uint64_t ddl_ns, int flags) {
  const char* p = (const char*)buf;
  size_t remaining = len;
  while (remaining > 0) {
    if (!wait_fd(fd, POLLOUT, ddl_ns)) {
      return false;
    }

    ssize_t written = send(fd, p, remaining, flags);
    if (written < 0) {
      if (errno == EINTR || errno == EAGAIN) {
        continue;
      }
      return false;
    }
    if (written == 0) {
      errno = EPIPE;
      return false;
    }

    p += written;
    remaining -= (size_t)written;
  }
  return true;
}

bool UBSocketIO::recv_all(int fd, void* buf, size_t len,
                          uint64_t ddl_ns, int flags) {
  char* p = (char*)buf;
  size_t remaining = len;
  while (remaining > 0) {
    if (!wait_fd(fd, POLLIN, ddl_ns)) {
      return false;
    }

    ssize_t nread = recv(fd, p, remaining, flags);
    if (nread < 0) {
      if (errno == EINTR || errno == EAGAIN) {
        continue;
      }
      return false;
    }
    if (nread == 0) {
      errno = ECONNRESET;
      return false;
    }

    p += nread;
    remaining -= (size_t)nread;
  }
  return true;
}
