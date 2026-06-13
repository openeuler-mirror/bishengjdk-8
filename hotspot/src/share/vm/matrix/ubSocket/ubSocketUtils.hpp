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

#ifndef SHARE_VM_MATRIX_UBSOCKETUTILS_HPP
#define SHARE_VM_MATRIX_UBSOCKETUTILS_HPP

#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "classfile/symbolTable.hpp"
#include "matrix/matrixUtils.hpp"
#include "runtime/interfaceSupport.hpp"
#include "runtime/mutex.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/thread.hpp"

class Monitor;
class UBSocketAttachSession;
struct UBSocketEndpoint;
struct UBSocketAttachFrame;

class UBSocketSessionCaches : public AllStatic {
 public:
  static void init();
  static void add(UBSocketAttachSession* session);
  static UBSocketAttachSession* find(const UBSocketEndpoint* local_ep,
                                     const UBSocketEndpoint* remote_ep);
  static UBSocketAttachSession* find(const UBSocketAttachFrame* request);
  static void release(UBSocketAttachSession* session);
  static void remove(const UBSocketEndpoint* local_ep, const UBSocketEndpoint* remote_ep);

 private:
  static Mutex* _cache_lock;
  static UBSocketAttachSession* _cache_head;
};

class UBSocketEndpointMap : public AllStatic {
 public:
  static void init();
  static int load_from_file(const char* conf_path);
  static void cleanup();
  static bool control_endpoint_for_data(const UBSocketEndpoint* data_ep,
                                        UBSocketEndpoint* control_ep);
  static bool has_mapping_for_data(const UBSocketEndpoint* data_ep);
  static bool matches_local_data(const UBSocketEndpoint* request_ep,
                                 const UBSocketEndpoint* session_ep);
};

class UBSocketEarlyReqQueue : public AllStatic {
 public:
  static void init() {_head = _tail = NULL; }
  static bool has_requests() { return _head != NULL; }
  static bool cache(int control_fd, const UBSocketAttachFrame* request, uint64_t ddl_ns);
  static int count();
  static bool take_one(int* control_fd, UBSocketAttachFrame* request, uint64_t* ddl_ns);
  static int cleanup();

 private:
  struct EarlyRequest;
  static EarlyRequest* _head;
  static EarlyRequest* _tail;
};

class UBSocketThreadUtils : public AllStatic {
 public:
  static JavaThread* start_daemon(ThreadFunction entry, const char* name,
                                  ThreadPriority priority = NoPriority);
};

#endif  // SHARE_VM_MATRIX_UBSOCKETUTILS_HPP
