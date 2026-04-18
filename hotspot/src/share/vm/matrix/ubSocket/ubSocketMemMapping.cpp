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

#include "matrix/ubSocket/ubSocketMemMapping.hpp"

#include <string.h>

#include "classfile/symbolTable.hpp"
#include "matrix/matrixLog.hpp"
#include "matrix/ubSocket/ubSocket.hpp"
#include "matrix/ubSocket/ubSocketUtils.hpp"
#include "runtime/mutex.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/thread.hpp"

class UBSocketMemMappingRegistry : public CHeapObj<mtInternal> {
 public:
  Monitor* lock;
  UBSocketMemMapping* head;

  UBSocketMemMappingRegistry()
    : lock(new Monitor(Mutex::leaf, "UBSocketMemMappingRegistry_lock")),
      head(NULL) {}
};

static UBSocketMemMappingRegistry* g_mem_mapping_registry = NULL;

static UBSocketMemMappingRegistry* mem_mapping_registry() {
  if (g_mem_mapping_registry == NULL) {
    g_mem_mapping_registry = new UBSocketMemMappingRegistry();
  }
  return g_mem_mapping_registry;
}

UBSocketMemMapping::UBSocketMemMapping(Symbol* name, size_t size, void* addr)
  : _name(name), _size(size), _addr(addr), _ref_count(1), _next(NULL) {}

int UBSocketMemMapping::decrement_ref() {
  guarantee(_ref_count > 0, "must be");
  return --_ref_count;
}

static UBSocketMemMapping* find_mapping_locked(UBSocketMemMappingRegistry* registry,
                                               Symbol* remote_name) {
  UBSocketMemMapping* cur = registry->head;
  while (cur != NULL) {
    if (cur->name() == remote_name) {
      return cur;
    }
    cur = cur->next();
  }
  return NULL;
}

static void remove_mapping_locked(UBSocketMemMappingRegistry* registry,
                                  UBSocketMemMapping* mapping) {
  UBSocketMemMapping* prev = NULL;
  UBSocketMemMapping* cur = registry->head;
  while (cur != NULL) {
    UBSocketMemMapping* next = cur->next();
    if (cur == mapping) {
      if (prev == NULL) {
        registry->head = next;
      } else {
        prev->set_next(next);
      }
      return;
    }
    prev = cur;
    cur = next;
  }
}

UBSocketMemMapping* UBSocketMemMapping::acquire(Symbol* remote_name, size_t remote_size) {
  UBSocketMemMappingRegistry* registry = mem_mapping_registry();
  {
    MutexLocker locker(registry->lock);
    UBSocketMemMapping* existing = find_mapping_locked(registry, remote_name);
    if (existing != NULL) {
      guarantee(existing->size() == remote_size, "UBSocketMemMapping size mismatch");
      existing->increment_ref();
      ResourceMark rm;
      UB_LOG(UB_SOCKET, UB_LOG_DEBUG, "remote=%s reuse addr=%p size=%lu ref=%d\n",
             remote_name->as_C_string(), existing->addr(),
             (unsigned long)remote_size, existing->ref_count());
      return existing;
    }
  }

  int error_code = 0;
  void* mapped_addr = os::Linux::ub_mmap(remote_name->as_C_string(), remote_size, &error_code);
  if (mapped_addr == NULL) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "remote=%s mmap failed size=%lu err=%d\n",
           remote_name->as_C_string(), (unsigned long)remote_size, error_code);
    return NULL;
  }

  UBSocketMemMapping* created = new (std::nothrow) UBSocketMemMapping(remote_name, remote_size, mapped_addr);
  if (created == NULL) {
    os::Linux::ub_munmap(mapped_addr, remote_size);
    errno = ENOMEM;
    return NULL;
  }

  {
    MutexLocker locker(registry->lock);
    UBSocketMemMapping* existing = find_mapping_locked(registry, remote_name);
    if (existing != NULL) {
      guarantee(existing->size() == remote_size, "UBSocketMemMapping size mismatch");
      existing->increment_ref();
      os::Linux::ub_munmap(mapped_addr, remote_size);
      ResourceMark rm;
      UB_LOG(UB_SOCKET, UB_LOG_DEBUG, "remote=%s reuse addr=%p size=%lu ref=%d\n",
             remote_name->as_C_string(), existing->addr(),
             (unsigned long)remote_size, existing->ref_count());
      delete created;
      return existing;
    }
    created->set_next(registry->head);
    registry->head = created;
  }

  ResourceMark rm;
  UB_LOG(UB_SOCKET, UB_LOG_DEBUG, "remote=%s mmap addr=%p size=%lu ref=1\n",
         remote_name->as_C_string(), mapped_addr, (unsigned long)remote_size);
  return created;
}

bool UBSocketMemMapping::release(int* ref_count_ptr) {
  UBSocketMemMappingRegistry* registry = mem_mapping_registry();
  int ref_count = 0;
  bool last_ref = false;

  {
    MutexLocker locker(registry->lock);
    ref_count = decrement_ref();
    last_ref = ref_count == 0;
    if (last_ref) {
      remove_mapping_locked(registry, this);
    }
  }

  if (ref_count_ptr != NULL) {
    *ref_count_ptr = ref_count;
  }
  if (!last_ref) {
    return false;
  }

  int error_code = os::Linux::ub_munmap(_addr, _size);
  if (error_code != 0) {
    UB_LOG(UB_SOCKET, UB_LOG_WARNING, "remote=%s munmap addr=%p size=%lu failed err=%d\n",
           _name->as_C_string(), _addr, (unsigned long)_size, error_code);
  } else {
    ResourceMark rm;
    UB_LOG(UB_SOCKET, UB_LOG_DEBUG, "remote=%s unmapped addr=%p size=%lu ref=0\n",
           _name->as_C_string(), _addr, (unsigned long)_size);
  }
  delete this;
  return true;
}

bool ub_socket_unbind_remote_mapping(int socket_fd, char* remote_mem_name,
                                     size_t remote_mem_name_len, int* ref_count_ptr) {
  UBSocketInfoList* info_list = SocketDataInfoTable::instance()->detach(socket_fd);
  if (info_list == NULL) {
    return false;
  }

  UBSocketMemMapping* mapping = info_list->get_mem_mapping();
  guarantee(mapping != NULL, "must be");
  ResourceMark rm;
  const char* mapping_name = mapping->name()->as_C_string();
  if (remote_mem_name != NULL && remote_mem_name_len > 0) {
    strncpy(remote_mem_name, mapping_name, remote_mem_name_len - 1);
    remote_mem_name[remote_mem_name_len - 1] = '\0';
  }
  if (UBSocketManager::package_timeout > 0) {
    UnreadMsgTable::instance()->unregister_fd(info_list->get_socket_fd());
  }
  delete info_list;
  mapping->release(ref_count_ptr);
  return true;
}
