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

#include "matrix/ubSocket/ubSocketMemMapping.hpp"

#include <string.h>

#include "classfile/symbolTable.hpp"
#include "matrix/matrixLog.hpp"
#include "matrix/ubSocket/ubSocketDataInfo.hpp"
#include "matrix/ubSocket/ubSocket.hpp"
#include "matrix/ubSocket/ubSocketUtils.hpp"
#include "runtime/mutex.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/thread.hpp"

Monitor* UBSocketMemMapping::_registry_lock = NULL;
UBSocketMemMapping* UBSocketMemMapping::_registry_head = NULL;

UBSocketMemMapping::UBSocketMemMapping(Symbol* name, size_t size, void* addr)
  : _name(name), _size(size), _addr(addr), _ref_count(1), _next(NULL) {}

void UBSocketMemMapping::init() {
  _registry_lock = new Monitor(Mutex::leaf, "UBSocketMemMapping_lock");
  _registry_head = NULL;
}

UBSocketMemMapping* UBSocketMemMapping::find_locked(Symbol* remote_name) {
  UBSocketMemMapping* cur = _registry_head;
  while (cur != NULL) {
    if (cur->name() == remote_name) { return cur; }
    cur = cur->next();
  }
  return NULL;
}

void UBSocketMemMapping::remove_locked(UBSocketMemMapping* mapping) {
  UBSocketMemMapping* prev = NULL;
  UBSocketMemMapping* cur = _registry_head;
  while (cur != NULL) {
    UBSocketMemMapping* next = cur->next();
    if (cur == mapping) {
      if (prev == NULL) {
        _registry_head = next;
      } else {
        prev->set_next(next);
      }
      return;
    }
    prev = cur;
    cur = next;
  }
}

UBSocketMemMapping* UBSocketMemMapping::acquire(const char* remote_name_str, size_t remote_size) {
  Symbol* remote_name = SymbolTable::new_symbol(remote_name_str, JavaThread::current());
  {
    MutexLocker locker(_registry_lock);
    UBSocketMemMapping* existing = find_locked(remote_name);
    if (existing != NULL) {
      if (existing->size() != remote_size) {
        UB_LOG(UB_SOCKET, UB_LOG_ERROR,
               "remote=%s mapping size mismatch existing=" SIZE_FORMAT
               " requested=" SIZE_FORMAT "\n",
               remote_name_str, existing->size(), remote_size);
        return NULL;
      }
      existing->increment_ref();
      UB_LOG(UB_SOCKET, UB_LOG_INFO,
             "remote=%s reuse addr=%p size=" SIZE_FORMAT " ref=%d\n",
             remote_name_str, existing->addr(), remote_size, existing->ref_count());
      return existing;
    }
  }

  int error_code = 0;
  // ub_mmap is costly, so call it after checking the registry for existing mapping without lock
  void* mapped_addr = os::Linux::ub_mmap(remote_name_str, remote_size, &error_code);
  if (mapped_addr == NULL) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING,
           "remote=%s mmap failed size=" SIZE_FORMAT " err=%d\n",
           remote_name_str, remote_size, error_code);
    return NULL;
  }

  UBSocketMemMapping* created =
      new (std::nothrow) UBSocketMemMapping(remote_name, remote_size, mapped_addr);
  if (created == NULL) {
    os::Linux::ub_munmap(mapped_addr, remote_size);
    errno = ENOMEM;
    return NULL;
  }

  {
    MutexLocker locker(_registry_lock);
    UBSocketMemMapping* existing = find_locked(remote_name);
    if (existing != NULL) {
      if (existing->size() != remote_size) {
        UB_LOG(UB_SOCKET, UB_LOG_ERROR,
               "remote=%s mapping size mismatch existing=" SIZE_FORMAT
               " requested=" SIZE_FORMAT "\n",
               remote_name_str, existing->size(), remote_size);
        os::Linux::ub_munmap(mapped_addr, remote_size);
        delete created;
        return NULL;
      }
      existing->increment_ref();
      os::Linux::ub_munmap(mapped_addr, remote_size);
      UB_LOG(UB_SOCKET, UB_LOG_INFO,
             "remote=%s reuse addr=%p size=" SIZE_FORMAT " ref=%d\n",
             remote_name_str, existing->addr(), remote_size, existing->ref_count());
      delete created;
      return existing;
    }
    created->set_next(_registry_head);
    _registry_head = created;
  }

  UB_LOG(UB_SOCKET, UB_LOG_INFO,
         "remote=%s mmap addr=%p size=" SIZE_FORMAT " ref=1\n",
         remote_name_str, mapped_addr, remote_size);
  return created;
}

bool UBSocketMemMapping::release() {
  bool last_ref = false;

  {
    MutexLocker locker(_registry_lock);
    int ref_count = decrement_ref();
    last_ref = ref_count == 0;
    if (last_ref) { remove_locked(this); }
  }
  if (!last_ref) { return false; }

  int error_code = os::Linux::ub_munmap(_addr, _size);
  if (error_code != 0) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING,
           "remote=%s munmap addr=%p size=" SIZE_FORMAT " failed err=%d\n",
           _name->as_C_string(), _addr, _size, error_code);
  } else {
    UB_LOG(UB_SOCKET, UB_LOG_INFO,
           "remote=%s unmapped addr=%p size=" SIZE_FORMAT " ref=0\n",
           _name->as_C_string(), _addr, _size);
  }
  return true;  // caller should delete this mapping
}

void UBSocketMemMapping::release_mapping(UBSocketMemMapping* mapping) {
  bool last_ref = mapping->release();
  if (last_ref) { delete mapping; }
}

bool UBSocketMemMapping::unbind(int fd) {
  UBSocketInfoList* info_list = SocketDataInfoTable::detach(fd);
  if (info_list == NULL) { return false; }

  UBSocketMemMapping* mapping = info_list->mapping();
  UnreadMsgTable::unregister_fd(fd);

  delete info_list;

  release_mapping(mapping);

  return true;
}
