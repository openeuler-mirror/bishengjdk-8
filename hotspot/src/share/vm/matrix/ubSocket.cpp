/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
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

#include "matrix/ubSocket.hpp"

#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "classfile/symbolTable.hpp"
#include "matrix/matrixManager.hpp"
#include "memory/resourceArea.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/thread.hpp"

/***************************const fields****************************/

static const uint64_t HEARTBEAT_MIN_TIMEOUT_MS = 100;
static const int SOCKET_MSG_FIELD_COUNT = 5;

/***************************static fields****************************/

pthread_mutex_t UBSocketManager::_mutex = PTHREAD_MUTEX_INITIALIZER;

Symbol *UBSocketManager::shared_memory_name = NULL;
void *UBSocketManager::shared_memory_addr = NULL;
uint64_t UBSocketManager::package_timeout = 0;
bool UBSocketManager::_initialized = false;
uint64_t UBSocketManager::_mem_blk_idx = 0;
size_t UBSocketManager::_blk_size = 1L * 1024 * 1024;
size_t UBSocketManager::_blk_meta_size = 16;
size_t UBSocketManager::_blk_count = 2048;

/************************UBSocketManager***************************/

void UBSocketManager::init() {
  if (!UseUBSocket) return;
  const char *mem_prefix = "Sock";
  enum { PREFIX_LEN = 4, HOST_LEN = 8, PID_LEN = 8 };
  enum { MEM_NAME_LEN = PREFIX_LEN + HOST_LEN + PID_LEN };
  char mem_name[MEM_NAME_LEN + 1];
  memcpy(mem_name, mem_prefix, PREFIX_LEN);

  char hostname[64] = {0};
  if (gethostname(hostname, sizeof(hostname)) != 0) {
    UB_LOG("ERROR", "UB Socket gethostname failed\n");
    return;
  }
  hostname[sizeof(hostname) - 1] = '\0';
  memset(mem_name + PREFIX_LEN, '_', HOST_LEN);
  size_t host_num = strnlen(hostname, sizeof(hostname));
  if (host_num >= HOST_LEN) {
    memcpy(mem_name + PREFIX_LEN, hostname + (host_num - HOST_LEN), HOST_LEN);
  } else {
    memcpy(mem_name + PREFIX_LEN + (HOST_LEN - host_num), hostname, host_num);
  }

  unsigned int pid = (unsigned int)getpid() % 100000000u;
  jio_snprintf(mem_name + PREFIX_LEN + HOST_LEN, PID_LEN + 1, "%0*u", PID_LEN, pid);
  mem_name[MEM_NAME_LEN] = '\0';
  UB_LOG("DEBUG", "UB Socket generate mem name: %s\n", mem_name);
  shared_memory_name = SymbolTable::new_symbol(mem_name, JavaThread::current());

  size_t total_size = _blk_size * _blk_count;
  int error_code = os::Linux::ub_malloc(mem_name, total_size);
  if (error_code != 0) {
    UB_LOG("ERROR", "UB Socket malloc failed %s, code %d\n", mem_name, error_code);
    return;
  }
  shared_memory_addr = os::Linux::ub_mmap(mem_name, total_size, &error_code);
  if (shared_memory_addr == NULL) {
    UB_LOG("ERROR", "UB Socket mmap failed %s, code %d\n", mem_name, error_code);
    return;
  }

  // init static singleton instance
  SocketDataInfoTable::init();
  SocketNameAddrTable::init();
  SocketBufferTable::init();
  UnreadMsgTable::init();

  if (UBSocketTimeout > 0 && UBSocketTimeout < HEARTBEAT_MIN_TIMEOUT_MS) {
    UBSocketTimeout = HEARTBEAT_MIN_TIMEOUT_MS;
    UB_LOG("WARNING", "UB Socket timeout is set to %d ms\n",
           HEARTBEAT_MIN_TIMEOUT_MS);
  }
  package_timeout = UBSocketTimeout * NANOSECS_PER_MILLISEC;
  if (package_timeout > 0) {
    UnreadMsgTable::instance()->start_timer();
  }

  _initialized = true;
}

void UBSocketManager::before_exit() {
  if (!UseUBSocket || !_initialized) return;
  UnreadMsgTable::instance()->stop_timer();
  int abnormal_fds = SocketDataInfoTable::instance()->unregister_abnormal_fds();
  if (abnormal_fds > 0)
    UB_LOG("WARNING", "UB Socket unregister %d abnormal fds\n", abnormal_fds);
  ResourceMark rm;
  os::Linux::ub_munmap(shared_memory_addr, _blk_size * _blk_count);
  os::Linux::ub_free(shared_memory_name->as_C_string());
  UB_LOG("DEBUG", "UB Socket clear memory %s\n", shared_memory_name->as_C_string());
}

void *UBSocketManager::get_free_memory(long len, long *offset, long *size) {
  if (!UseUBSocket || !_initialized || len <= 0) return NULL;
  long transport_size = len + _blk_meta_size;
  size_t blk_need = (transport_size + _blk_meta_size + _blk_size - 1) / _blk_size;
  if (blk_need > _blk_count) {
    UB_LOG("ERROR", "Socket get memory len %ld need %ld blk overflow count %ld\n",
           len, blk_need, _blk_count);
    guarantee(false, "UB socket need mem blk overflow");
  }
  if (blk_need > 1) {
    UB_LOG("WARNING", "Socket get memory len %ld need %ld blk > 1\n", len, blk_need);
  }
  int last_index = Atomic::add(blk_need, &_mem_blk_idx) % _blk_count;
  int index = last_index - blk_need;
  if (index < 0) {
    index = (index + _blk_count) % _blk_count;
    // truncate current block for memory continuity
    blk_need = _blk_count - index;
  }
  uintptr_t addr = uintptr_t(shared_memory_addr) + _blk_size * index;
  // todo: check has been read?
  mark((void *)addr, uint32_t(blk_need));
  uintptr_t data_addr = addr + _blk_meta_size;

  *offset = _blk_size * index + _blk_meta_size;
  *size = _blk_size * blk_need - _blk_meta_size;
  UB_LOG("DEBUG", "UB Socket get free memory %p offset %ld size %ld len %ld\n",
         (void *)addr, *offset, *size, len);
  return (void *)data_addr;
}

uint32_t fnv1a_hash(char *str) {
  uint32_t hash = 2166136261U;
  while (*str != '\0') {
    hash ^= (uint8_t)*str++;
    hash *= 16777619U;
  }
  return hash;
}

int get_socket_port(int fd, int *local_port_addr, int *remote_port_addr) {
  if (!PrintUBLog) return 0;

  struct sockaddr_storage sock_addr;
  socklen_t sock_len;
  int local_port = 0, remote_port = 0;
  sock_len = sizeof(sock_addr);
  if (getsockname(fd, (struct sockaddr *)&sock_addr, &sock_len) == 0) {
    if (sock_addr.ss_family == AF_INET) {
      struct sockaddr_in *in = (struct sockaddr_in *)&sock_addr;
      local_port = ntohs(in->sin_port);
    } else if (sock_addr.ss_family == AF_INET6) {
      struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)&sock_addr;
      local_port = ntohs(in6->sin6_port);
    }
    *local_port_addr = local_port;
  } else {
    UB_LOG("ERROR", "UB Socket %d get local port error\n", fd);
    return -1;
  }

  sock_len = sizeof(sock_addr);
  if (getpeername(fd, (struct sockaddr *)&sock_addr, &sock_len) == 0) {
    if (sock_addr.ss_family == AF_INET) {
      struct sockaddr_in *in = (struct sockaddr_in *)&sock_addr;
      remote_port = ntohs(in->sin_port);
    } else if (sock_addr.ss_family == AF_INET6) {
      struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)&sock_addr;
      remote_port = ntohs(in6->sin6_port);
    }
    *remote_port_addr = remote_port;
  } else {
    UB_LOG("ERROR", "UB Socket %d get remote port error\n", fd);
    return -1;
  }

  return 0;
}

long UBSocketManager::write_data(void *buf, int socket_fd, long len) {
  if (!UseUBSocket || !_initialized || len <= 0) return 0;
  long ub_offset, ub_size;
  void *socket_addr = get_free_memory(len, &ub_offset, &ub_size);
  if (socket_addr == NULL) {
    UB_LOG("ERROR", "UB Socket %d get free memory error, %p len %ld\n",
           socket_fd, buf, len);
    // need new func to handle get_free_memory error
    guarantee(false, "must be");
  }

  long write_size = ub_size < len ? ub_size : len;
  memcpy(socket_addr, buf, write_size);
  int nsend = send_msg(socket_fd, socket_addr, ub_offset, write_size);
  if (nsend <= 0) {
    UB_LOG("WARNING", "UB Socket %d len %ld send msg failed, res %d\n",
           socket_fd, len, nsend);
    return nsend;
  }
  if (package_timeout > 0) {
    uintptr_t meta_addr = uintptr_t(socket_addr) - _blk_meta_size;
    UnreadMsgTable::instance()->add_msg(socket_fd, (void *)meta_addr);
  }

  if (write_size == len) return len;
  // Need write & send remain data
  UB_LOG("WARNING", "UB Socket %d need get free memory twice, len %ld but ub_size %ld\n",
         socket_fd, len, ub_size);
  uintptr_t next_buf = (uintptr_t)buf + write_size;
  return ub_size + write_data((void *)next_buf, socket_fd, len - write_size);
}

int UBSocketManager::send_msg(int socket_fd, void *socket_addr, long ub_offset,
                              long len) {
  if (!UseUBSocket || !_initialized || len == 0) return false;
  ResourceMark rm;
  char ub_core_msg[64];
  jio_snprintf(ub_core_msg, sizeof(ub_core_msg), "%s:%ld:%ld",
               shared_memory_name->as_C_string(), ub_offset, len);
  uint32_t fnv1a_hashcode = fnv1a_hash(ub_core_msg);
  char ub_msg[128];
  jio_snprintf(ub_msg, sizeof(ub_msg), "%u|%s|%u;", fnv1a_hashcode, ub_core_msg, fnv1a_hashcode);
  int nwrite = write(socket_fd, ub_msg, strlen(ub_msg));
  int local_port, remote_port;
  get_socket_port(socket_fd, &local_port, &remote_port);
  UB_LOG("DEBUG", "UB Socket %d(local %d - remote %d) send msg %s data %p res %d\n",
         socket_fd, local_port, remote_port, ub_msg, socket_addr, nwrite);
  return nwrite;
}

int UBSocketManager::send_heartbeat(int socket_fd) {
  if (!UseUBSocket || !_initialized) return false;
  char ub_core_msg[64];
  jio_snprintf(ub_core_msg, sizeof(ub_core_msg), "%s:%d:%d", "Heartbeat", 0, 0);
  uint32_t fnv1a_hashcode = 0;
  char ub_msg[128];
  jio_snprintf(ub_msg, sizeof(ub_msg), "%u|%s|%u;", fnv1a_hashcode, ub_core_msg, fnv1a_hashcode);
  int nwrite = write(socket_fd, ub_msg, strlen(ub_msg));
  UB_LOG("DEBUG", "UB Socket %d send heartbeat, nwrite %d\n", socket_fd, nwrite);
  return nwrite;
}

long UBSocketManager::parse_msg(int socket_fd, char *ub_msg_chain) {
  if (!UseUBSocket || !_initialized) return 0;
  int local_port, remote_port;
  get_socket_port(socket_fd, &local_port, &remote_port);
  UB_LOG("DEBUG", "UB Socket parse msg %s len %d from fd %d(local %d - remote %d)\n",
         ub_msg_chain, strlen(ub_msg_chain), socket_fd, local_port, remote_port);
  char *start_pos = ub_msg_chain;
  int total_parse_size = 0;
  int read_count;
  while (*start_pos) {
    char *end_pos = strchr(start_pos, ';');
    if (end_pos == NULL) {
      UB_LOG("WARNING", "UB Socket parse msg %s failed, fd %d need save to buffer\n",
             start_pos, socket_fd);
      SocketBufferTable::instance()->update_buffer(socket_fd, start_pos);
      return total_parse_size;
    }
    char socket_mem_name[64];
    long socket_offset, socket_size;
    uint32_t checksum_top, checksum_end;
    int parsed = sscanf(start_pos, "%u|%64[^:]:%ld:%ld|%u;", &checksum_top,
                        socket_mem_name, &socket_offset, &socket_size, &checksum_end);
    if (parsed != SOCKET_MSG_FIELD_COUNT || checksum_top != checksum_end) {
      int cur_msg_len = int(end_pos - start_pos) + 1;
      UB_LOG("WARNING", "UB Socket parse msg %.*s failed, fd %d need read buffer\n",
             cur_msg_len, start_pos, socket_fd);
      char *ub_msg_buf = (char *)malloc(128);
      int last_msg_len =  SocketBufferTable::instance()->read_buffer(socket_fd, ub_msg_buf);
      strncpy(ub_msg_buf + last_msg_len, start_pos, cur_msg_len);
      parsed = sscanf(ub_msg_buf, "%u|%64[^:]:%ld:%ld|%u;", &checksum_top,
                      socket_mem_name, &socket_offset, &socket_size, &checksum_end);
      guarantee(parsed == SOCKET_MSG_FIELD_COUNT && checksum_top == checksum_end, "must be");
      free(ub_msg_buf);
    }
    socket_mem_name[strlen(socket_mem_name)] = '\0';
    UB_LOG("DEBUG", "UB Socket parse msg name %s offset %ld size %ld crc %u\n",
           socket_mem_name, socket_offset, socket_size, checksum_top);
    if (socket_size > 0) {
      // ignore heartbeat
      jlong buf_size = buffer_data(socket_fd, socket_mem_name, socket_offset, socket_size);
      total_parse_size += buf_size;
    }
    start_pos = end_pos + 1;
  }
  return total_parse_size;
}

long UBSocketManager::buffer_data(int socket_fd, char *name, long off, long len) {
  if (!UseUBSocket || !_initialized) return 0;
  Symbol *name_symbol = SymbolTable::new_symbol(name, JavaThread::current());
  void *socket_memory_addr = SocketNameAddrTable::instance()->get(name_symbol);
  guarantee(socket_memory_addr != NULL, "must be");
  UBSocketInfoList *info_list = SocketDataInfoTable::instance()->get(socket_fd);
  guarantee(info_list != NULL, "must be");
  info_list->append(off, len);
  UB_LOG("DEBUG", "UB Socket buffer %p(%s) add offset %ld len %ld from socket %d\n",
         socket_memory_addr, name, off, len, socket_fd);
  return len;
}

long UBSocketManager::read_data(void *buf, int socket_fd, long len) {
  if (!UseUBSocket || !_initialized) return 0;
  UBSocketInfoList **list_addr = SocketDataInfoTable::instance()->lookup(socket_fd);
  guarantee(list_addr != NULL, "must be");
  UBSocketInfoList *info_list = *list_addr;
  size_t nread = info_list->read_data(buf, len);
  int local_port, remote_port;
  get_socket_port(socket_fd, &local_port, &remote_port);
  UB_LOG("DEBUG", "UB Socket buffer read socket %d(%d - %d) len %ld to %p, nread %ld\n",
         socket_fd, local_port, remote_port, len, buf, nread);
  return nread;
}

int write_full(int fd, const void *buf, size_t n) {
  const uint8_t *p = (const uint8_t *)buf;
  size_t left = n;
  while (left > 0) {
    ssize_t w = write(fd, p, left);
    if (w > 0) {
      p += (size_t)w;
      left -= (size_t)w;
      continue;
    }
    if (w == 0) return -1;
    if (errno == EINTR) continue;
    return -1;
  }
  return 0;
}

int read_full(int fd, void *buf, size_t n, int timeout_ms) {
  uint8_t *p = (uint8_t *)buf;
  size_t left = n;
  while (left > 0) {
    ssize_t r = read(fd, p, left);
    if (r > 0) {
      p += (size_t)r;
      left -= (size_t)r;
      continue;
    }
    if (r == 0) return -1;
    if (errno == EINTR) continue;
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      struct pollfd fds = {.fd = fd, .events = POLLIN};
      int pr = poll(&fds, 1, timeout_ms);
      if (pr > 0) continue;
      if (pr == 0) {
        errno = ETIMEDOUT;
        return -1;
      }
      return -1;
    }
    return -1;
  }
  return 0;
}

bool UBSocketManager::register_fd(int socket_fd) {
  if (!UseUBSocket || !_initialized) return false;
  long start_time = os::javaTimeNanos();
  if (has_registered(socket_fd)) {
    UB_LOG("WARNING", "Socket %d has been registered\n", socket_fd);
    return true;
  }
  ResourceMark rm;
  int mem_name_len = shared_memory_name->utf8_length();
  int error_code = write_full(socket_fd, shared_memory_name->as_C_string(), mem_name_len);
  if (error_code != 0) {
    UB_LOG("ERROR", "Send shared_memory_name to %d failed when connecting\n", socket_fd);
    return false;
  }
  char remote_memory_name[mem_name_len + 1];
  error_code = read_full(socket_fd, remote_memory_name, mem_name_len,
                         HEARTBEAT_MIN_TIMEOUT_MS);
  if (error_code != 0) {
    UB_LOG("ERROR", "Recv remote_memory_name from %d failed when connecting\n", socket_fd);
    return false;
  }
  remote_memory_name[mem_name_len] = '\0';
  void *remote_memory_addr = os::Linux::ub_mmap(remote_memory_name, _blk_size * _blk_count, &error_code);
  if (remote_memory_addr == NULL) {
    UB_LOG("ERROR", "UB socket mmap %s failed(%d), fallback to TCP\n",
           remote_memory_name, error_code);
    return false;
  }
  Symbol *remote_name_symbol = SymbolTable::new_symbol(remote_memory_name, JavaThread::current());
  SocketNameAddrTable::instance()->add(remote_name_symbol, remote_memory_addr);
  UBSocketInfoList *info_list = new UBSocketInfoList(socket_fd);
  info_list->set_mem_addr(remote_memory_addr);
  info_list->set_mem_name(remote_name_symbol);
  SocketDataInfoTable::instance()->add(socket_fd, info_list);
  if (package_timeout > 0) {
    UnreadMsgTable::instance()->register_fd(socket_fd);
  }
  long cost_time = os::javaTimeNanos() - start_time;
  UB_LOG("DEBUG", "UB Socket register fd %d remote %s(%p), cost %ld ns\n",
         socket_fd, remote_memory_name, remote_memory_addr, cost_time);
  return true;
}

bool UBSocketManager::unregister_fd(int socket_fd) {
  if (!UseUBSocket || !_initialized) return false;
  long start_time = os::javaTimeNanos();
  if (!has_registered(socket_fd)) {
    UB_LOG("WARNING", "Socket %d has been unregistered\n", socket_fd);
    return true;
  }
  ResourceMark rm;
  UBSocketInfoList *info_list = SocketDataInfoTable::instance()->get(socket_fd);
  guarantee(info_list != NULL, "must be");
  char *remote_memory_name = info_list->get_mem_name()->as_C_string();
  void *remote_memory_addr = info_list->get_mem_addr();
  int error_code = os::Linux::ub_munmap(remote_memory_addr, _blk_size * _blk_count);
  if (error_code != 0) {
    UB_LOG("WARNING", "UB socket munmap %s(%p, %ld) failed %d\n",
           remote_memory_name, remote_memory_addr, _blk_size * _blk_count, error_code);
  }
  SocketNameAddrTable::instance()->remove(info_list->get_mem_name());
  SocketDataInfoTable::instance()->remove(socket_fd);
  if (package_timeout > 0) {
    UnreadMsgTable::instance()->unregister_fd(socket_fd);
  }
  long cost_time = os::javaTimeNanos() - start_time;
  UB_LOG("DEBUG", "UB Socket unregister %d remote %s, cost %ld ns\n", socket_fd,
         remote_memory_name, cost_time);
  return true;
}

bool UBSocketManager::has_registered(int socket_fd) {
  if (!UseUBSocket || !_initialized) return false;
  return SocketDataInfoTable::instance()->get(socket_fd) != NULL;
}