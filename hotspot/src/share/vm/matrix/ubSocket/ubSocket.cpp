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

#include <string.h>
#include <unistd.h>

#include "classfile/symbolTable.hpp"
#include "matrix/matrixLog.hpp"
#include "matrix/ubSocket/ubSocketAttach.hpp"
#include "matrix/ubSocket/ubSocketIO.hpp"
#include "matrix/ubSocket/ubSocketMemMapping.hpp"
#include "matrix/ubSocket/ubSocketUtils.hpp"
#include "memory/resourceArea.hpp"
#include "runtime/thread.hpp"

#include "matrix/ubSocket/ubSocket.hpp"

static const uint64_t HEARTBEAT_MIN_TIMEOUT_MS = 100;
static const int SOCKET_DESCRIPTOR_FIELD_COUNT = 3;
static const size_t UB_SOCKET_DEFAULT_BLOCK_SIZE = 4L * K;
static const size_t UB_SOCKET_DEFAULT_BLOCK_COUNT = 32L * K; // 128M total
static const size_t UB_SOCKET_DESCRIPTOR_BUF_LEN = 128;
static const char* UB_SOCKET_HEARTBEAT_NAME = "Heartbeat";

Symbol *UBSocketManager::shared_memory_name = NULL;
void *UBSocketManager::shared_memory_addr = NULL;
uint64_t UBSocketManager::package_timeout = 0;
bool UBSocketManager::_initialized = false;
uint64_t UBSocketManager::_mem_blk_idx = 0;
size_t UBSocketManager::_blk_size = UB_SOCKET_DEFAULT_BLOCK_SIZE;
size_t UBSocketManager::_blk_meta_size = sizeof(UBSocketBlkMeta);
size_t UBSocketManager::_blk_count = UB_SOCKET_DEFAULT_BLOCK_COUNT;

AllowListTable* UBSocketManager::_allow_list_table = NULL;

void UBSocketManager::init() {
  if (!UseUBSocket) return;
  
  if (ub_option_blank(UBSocketConfPath)) {
    tty->print_cr("conf path is NULL, is disabled.");
    return;
  }
  
  _allow_list_table = new AllowListTable(UB_SOCKET);
  if (_allow_list_table->load_from_file(UBSocketConfPath) == 0) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "Load allow-list failed or empty: %s\n", UBSocketConfPath);
    return;
  }

  if (UBSocketTimeout != 0 && UBSocketTimeout < HEARTBEAT_MIN_TIMEOUT_MS) {
    tty->print_cr("timeout(%ld) invalid, set to %ld ms\n",
                  UBSocketTimeout, HEARTBEAT_MIN_TIMEOUT_MS);
    UBSocketTimeout = HEARTBEAT_MIN_TIMEOUT_MS;
  }

  char mem_name[UB_SOCKET_MEM_NAME_BUF_LEN];
  memcpy(mem_name, UB_SOCKET_MEM_PREFIX, UB_SOCKET_MEM_PREFIX_LEN);

  char hostname[UB_SOCKET_DESCRIPTOR_BUF_LEN] = {0};
  if (gethostname(hostname, sizeof(hostname)) != 0) {
    UB_LOG(UB_SOCKET, UB_LOG_ERROR, "gethostname failed\n");
    return;
  }
  hostname[sizeof(hostname) - 1] = '\0';
  memset(mem_name + UB_SOCKET_MEM_PREFIX_LEN, '_', UB_SOCKET_MEM_HOST_LEN);
  size_t host_num = strnlen(hostname, sizeof(hostname));
  if (host_num >= UB_SOCKET_MEM_HOST_LEN) {
    memcpy(mem_name + UB_SOCKET_MEM_PREFIX_LEN,
           hostname + (host_num - UB_SOCKET_MEM_HOST_LEN), UB_SOCKET_MEM_HOST_LEN);
  } else {
    memcpy(mem_name + UB_SOCKET_MEM_PREFIX_LEN + (UB_SOCKET_MEM_HOST_LEN - host_num), hostname, host_num);
  }

  unsigned int pid = (unsigned int)getpid() % 100000000u;
  char* pid_name = mem_name + UB_SOCKET_MEM_PREFIX_LEN + UB_SOCKET_MEM_HOST_LEN;
  jio_snprintf(pid_name, UB_SOCKET_MEM_PID_LEN + 1, "%0*u", (int)UB_SOCKET_MEM_PID_LEN, pid);
  mem_name[UB_SOCKET_MEM_NAME_LEN] = '\0';
  int error_code = os::Linux::ub_malloc(mem_name, _blk_size * _blk_count);
  if (error_code != 0) {
    UB_LOG(UB_SOCKET, UB_LOG_ERROR, "init malloc failed name=%s err=%d\n", mem_name, error_code);
    return;
  }
  shared_memory_name = SymbolTable::new_symbol(mem_name, JavaThread::current());
  shared_memory_addr = os::Linux::ub_mmap(mem_name, _blk_size * _blk_count, &error_code);
  if (shared_memory_addr == NULL) {
    UB_LOG(UB_SOCKET, UB_LOG_ERROR, "init mmap failed name=%s err=%d\n", mem_name, error_code);
    clean_ub_resources();
    return;
  }

  SocketDataInfoTable::init();
  SocketBufferTable::init();
  UnreadMsgTable::init();
  package_timeout = UBSocketTimeout * NANOSECS_PER_MILLISEC;
  if (package_timeout > 0) {
    UnreadMsgTable::instance()->start_timer();
  }

  _initialized = true;
}

void UBSocketManager::clean_ub_resources() {
  if (shared_memory_addr != NULL) {
    int error_code = os::Linux::ub_munmap(shared_memory_addr, _blk_size * _blk_count);
    if (error_code != 0) {
      UB_LOG(UB_SOCKET, UB_LOG_WARNING, "cleanup munmap failed addr=%p err=%d\n",
             shared_memory_addr, error_code);
    }
    shared_memory_addr = NULL;
  }
  if (shared_memory_name != NULL) {
    ResourceMark rm;
    const char* mem_name = shared_memory_name->as_C_string();
    int error_code = os::Linux::ub_free(mem_name);
    if (error_code != 0) {
      UB_LOG(UB_SOCKET, UB_LOG_WARNING, "cleanup free failed name=%s err=%d\n", mem_name, error_code);
    }
    shared_memory_name = NULL;
  }
}

void UBSocketManager::before_exit() {
  if (!UseUBSocket || !_initialized) return;
  if (package_timeout > 0) {
    UnreadMsgTable::instance()->stop_timer();
    UnreadMsgTable::instance()->cleanup();
  }
  UBSocketAttachAgent::shutdown();
  int abnormal_fds = SocketDataInfoTable::instance()->unregister_abnormal_fds();
  if (abnormal_fds > 0) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "shutdown cleaned %d abnormal fds\n", abnormal_fds);
  }
  clean_ub_resources();
}

bool UBSocketManager::check_stack() {
  if (!_initialized) return false;
  if (_allow_list_table == NULL) return false;
  return _allow_list_table->check_stack();
}

void *UBSocketManager::get_free_memory(long len, long *offset, long *size) {
  if (!UseUBSocket || !_initialized || len <= 0) return NULL;
  long transport_size = len + _blk_meta_size;
  size_t blk_need = (transport_size + _blk_meta_size + _blk_size - 1) / _blk_size;
  if (blk_need > _blk_count) {
    UB_LOG(UB_SOCKET, UB_LOG_ERROR, "alloc blocks overflow len=%ld need=%ld total=%ld\n",
           len, blk_need, _blk_count);
    guarantee(false, "need mem blk overflow");
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
  return (void *)data_addr;
}

long UBSocketManager::write_data(void *buf, int socket_fd, long len) {
  if (!UseUBSocket || !_initialized || len <= 0) return 0;
  long ub_offset, ub_size;
  void *socket_addr = get_free_memory(len, &ub_offset, &ub_size);
  if (socket_addr == NULL) {
    UB_LOG(UB_SOCKET, UB_LOG_ERROR, "fd=%d alloc send buffer failed len=%ld\n", socket_fd, len);
    // need new func to handle get_free_memory error
    guarantee(false, "must be");
  }

  long write_size = ub_size < len ? ub_size : len;
  memcpy(socket_addr, buf, write_size);
  int nsend = send_msg(socket_fd, socket_addr, ub_offset, write_size);
  if (nsend <= 0) {
    clean_mark((uintptr_t)socket_addr);
    UB_LOG(UB_SOCKET, UB_LOG_ERROR, "fd=%d send descriptor failed len=%ld rc=%d\n",
           socket_fd, len, nsend);
    return nsend;
  }
  if (package_timeout > 0) {
    uintptr_t meta_addr = uintptr_t(socket_addr) - _blk_meta_size;
    UnreadMsgTable::instance()->add_msg(socket_fd, (void *)meta_addr);
  }

  if (write_size == len) return len;
  uintptr_t next_buf = (uintptr_t)buf + write_size;
  return ub_size + write_data((void *)next_buf, socket_fd, len - write_size);
}

int UBSocketManager::send_msg(int fd, void* addr, long ub_offset, long len) {
  if (!UseUBSocket || !_initialized || len == 0) return false;
  ResourceMark rm;
  char ub_core_msg[UB_SOCKET_DESCRIPTOR_BUF_LEN];
  jio_snprintf(ub_core_msg, sizeof(ub_core_msg), "%s:%ld:%ld",
               shared_memory_name->as_C_string(), ub_offset, len);
  char ub_msg[UB_SOCKET_DESCRIPTOR_BUF_LEN];
  jio_snprintf(ub_msg, sizeof(ub_msg), "%s;", ub_core_msg);
  return (int)UBSocketIO::write(fd, ub_msg, strlen(ub_msg));
}

int UBSocketManager::send_heartbeat(int socket_fd) {
  if (!UseUBSocket || !_initialized) return false;
  char ub_core_msg[UB_SOCKET_DESCRIPTOR_BUF_LEN];
  jio_snprintf(ub_core_msg, sizeof(ub_core_msg), "%s:%d:%d",
               UB_SOCKET_HEARTBEAT_NAME, 0, 0);
  char ub_msg[UB_SOCKET_DESCRIPTOR_BUF_LEN];
  jio_snprintf(ub_msg, sizeof(ub_msg), "%s;", ub_core_msg);
  return (int)UBSocketIO::write(socket_fd, ub_msg, strlen(ub_msg));
}

static bool ub_socket_extract_descriptors(int socket_fd, const char* ub_msg, size_t ub_msg_len,
                                          char** descriptors, size_t* available) {
  *descriptors = NULL;
  *available = 0;
  if (!SocketBufferTable::instance()->extract_complete_descriptors(socket_fd, ub_msg, ub_msg_len,
                                                                   descriptors, available)) {
    UB_LOG(UB_SOCKET, UB_LOG_ERROR, "append descriptor buffer failed on fd %d, len %lu\n",
           socket_fd, (unsigned long)ub_msg_len);
    return false;
  }
  return true;
}

static long ub_socket_parse_one_descriptor(int socket_fd, const char* descriptor,
                                           size_t descriptor_len) {
  if (descriptor_len == 0 || descriptor_len >= UB_SOCKET_DESCRIPTOR_BUF_LEN) {
    UB_LOG(UB_SOCKET, UB_LOG_ERROR, "fd=%d parse descriptor failed len=%lu\n",
           socket_fd, (unsigned long)descriptor_len);
    return 0;
  }

  char descriptor_buf[UB_SOCKET_DESCRIPTOR_BUF_LEN] = {0};
  memcpy(descriptor_buf, descriptor, descriptor_len);

  char socket_mem_name[UB_SOCKET_DESCRIPTOR_BUF_LEN];
  long socket_offset = 0;
  long socket_size = 0;
  int parsed = sscanf(descriptor_buf, "%63[^:]:%ld:%ld",
                      socket_mem_name, &socket_offset, &socket_size);
  if (parsed != SOCKET_DESCRIPTOR_FIELD_COUNT) {
    UB_LOG(UB_SOCKET, UB_LOG_ERROR, "fd=%d parse descriptor failed len=%lu\n",
           socket_fd, (unsigned long)descriptor_len);
    return 0;
  }
  if (socket_size <= 0) { return 0; }
  return UBSocketManager::buffer_data(socket_fd, socket_mem_name, socket_offset, socket_size);
}

long UBSocketManager::parse_msg(int socket_fd, const char* ub_msg, size_t ub_msg_len) {
  if (!UseUBSocket || !_initialized || ub_msg_len == 0) return 0;

  char* descriptors = NULL;
  size_t available = 0;
  if (!ub_socket_extract_descriptors(socket_fd, ub_msg, ub_msg_len, &descriptors, &available)) {
    return 0;
  }
  if (descriptors == NULL || available == 0) {
    free(descriptors);
    return 0;
  }

  size_t consumed = 0;
  long total_parse_size = 0;
  while (consumed < available) {
    const char* start_pos = descriptors + consumed;
    const char* end_pos = (const char*)memchr(start_pos, ';', available - consumed);
    if (end_pos == NULL) break;
    size_t descriptor_len = (size_t)(end_pos - start_pos);
    total_parse_size += ub_socket_parse_one_descriptor(socket_fd, start_pos,
                                                       descriptor_len);
    consumed += descriptor_len + 1;
  }

  free(descriptors);
  return total_parse_size;
}

long UBSocketManager::buffer_data(int socket_fd, char *name, long off, long len) {
  if (!UseUBSocket || !_initialized) return 0;
  if (!SocketDataInfoTable::instance()->append_range(socket_fd, off, len)) {
    return 0;
  }
  return len;
}

long UBSocketManager::read_data(void *buf, int socket_fd, long len) {
  if (!UseUBSocket || !_initialized) return 0;
  return SocketDataInfoTable::instance()->read_data(socket_fd, buf, len);
}

bool UBSocketManager::register_fd(int socket_fd, bool is_server) {
  if (!UseUBSocket || !_initialized) return false;
  long start_time = os::javaTimeNanos();

  if (has_registered(socket_fd)) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "fd=%d register skipped: already registered\n", socket_fd);
    return true;
  }

  UBSocketAttach socket_attach(socket_fd, is_server, shared_memory_name,
                               _blk_size * _blk_count);
  bool attach_ok = socket_attach.do_attach();
  if (!attach_ok) {
    if (has_registered(socket_fd)) { unregister_fd(socket_fd); }
    return false;
  }

  long cost_time = os::javaTimeNanos() - start_time;
  UB_LOG(UB_SOCKET, UB_LOG_DEBUG, "fd=%d register success cost_ns=%ld\n", socket_fd, cost_time);
  return true;
}

bool UBSocketManager::unregister_fd(int socket_fd) {
  if (!UseUBSocket || !_initialized) return false;
  long start_time = os::javaTimeNanos();
  if (!has_registered(socket_fd)) {
    return false;
  }
  char remote_memory_name[UB_SOCKET_MEM_NAME_BUF_LEN] = {0};
  size_t remote_memory_size = _blk_size * _blk_count;
  int ref_count = 0;
  bool unbound = ub_socket_unbind_remote_mapping(socket_fd, remote_memory_name,
                                                 sizeof(remote_memory_name), &ref_count);
  guarantee(unbound, "must be");
  SocketBufferTable::instance()->free_buffer(socket_fd);
  long cost_time = os::javaTimeNanos() - start_time;
  UB_LOG(UB_SOCKET, UB_LOG_DEBUG, "fd=%d unregister remote=%s ref=%d cost_ns=%ld\n",
         socket_fd, remote_memory_name, ref_count, cost_time);
  return true;
}

bool UBSocketManager::has_registered(int socket_fd) {
  if (!UseUBSocket || !_initialized) return false;
  return SocketDataInfoTable::instance()->contains(socket_fd);
}

bool UBSocketManager::wait_fd_ready(int socket_fd) {
  return UseUBSocket && _initialized && has_registered(socket_fd);
}
