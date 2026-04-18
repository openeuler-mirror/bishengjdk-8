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
#include <string.h>

#include "classfile/symbolTable.hpp"
#include "matrix/matrixUtils.hpp"
#include "runtime/interfaceSupport.hpp"
#include "runtime/mutex.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/thread.hpp"

class Monitor;
class UBSocketAttachSession;
class UBSocketMemMapping;
struct UBSocketEndpoint;
struct UBSocketControlFrame;

class UBSocketThreadUtils : public AllStatic {
 public:
  static JavaThread* start_daemon(ThreadFunction entry, const char* name,
                                  ThreadPriority priority = NoPriority);
};

class UBSocketInfoList : public CHeapObj<mtInternal> {
 public:
  void append(size_t offset, size_t size);  // cache <off,len> for socket data
  size_t read_data(void* dst, size_t len);  // read len data of this fd to dst

  int get_socket_fd() { return _socket_fd; }
  void* get_mem_addr() { return _mem_addr; }
  void set_mem_addr(void* addr) { _mem_addr = addr; }
  Symbol* get_mem_name() { return _mem_name; }
  void set_mem_name(Symbol* name) { _mem_name = name; }
  UBSocketMemMapping* get_mem_mapping() { return _mem_mapping; }
  void set_mem_mapping(UBSocketMemMapping* mapping) { _mem_mapping = mapping; }

  explicit UBSocketInfoList(int fd)
      : _head(NULL), _tail(NULL), _cursor(NULL),
        _cur_loc(0), _socket_fd(fd),
        _mem_addr(NULL), _mem_name(NULL), _mem_mapping(NULL) {}
  ~UBSocketInfoList() {
    while (_head) {
      SocketListNode* next = _head->next;
      delete _head;
      _head = next;
    }
  }

 private:
  class SocketListNode : public CHeapObj<mtInternal> {
   public:
    SocketListNode(size_t off, size_t size, UBSocketInfoList::SocketListNode* n)
        : offset(off), size(size), next(n) {}

    size_t offset;
    size_t size;
    SocketListNode* next;
  };
  SocketListNode* _head;
  SocketListNode* _tail;
  SocketListNode* _cursor;  // current node
  size_t _cur_loc;          // current node location

  int _socket_fd;     // one instance pre socket
  void* _mem_addr;    // remote mem addr
  Symbol* _mem_name;  // remote mem name
  UBSocketMemMapping* _mem_mapping;  // lifecycle owner for the remote mapping

  int delete_nodes(SocketListNode* start, SocketListNode* end);
};

class SocketDataInfoTable : public CHeapObj<mtInternal> {
 public:
  static void init();
  static SocketDataInfoTable* instance() { return _instance; }

  void publish(int fd, UBSocketInfoList* info);
  bool contains(int fd);
  bool append_range(int fd, size_t offset, size_t size);
  size_t read_data(int fd, void* dst, size_t len);
  UBSocketInfoList* detach(int fd);
  int unregister_abnormal_fds();  // unregister fds abnormal exit

 private:
  static Monitor* _table_lock;
  static SocketDataInfoTable* _instance;
  PtrTable<int, UBSocketInfoList*, mtInternal> _table;
  SocketDataInfoTable() : _table(NULL) {}
};

class SocketDescriptorBuffer : public CHeapObj<mtInternal> {
 public:
  SocketDescriptorBuffer() : _data(NULL), _len(0), _cap(0) {}
  ~SocketDescriptorBuffer() {
    if (_data != NULL) {
      free(_data);
    }
  }

  bool append(const char* msg, size_t len);
  void consume(size_t len);

  char* data() const { return _data; }
  size_t len() const { return _len; }

 private:
  char* _data;
  size_t _len;
  size_t _cap;
};

class SocketBufferTable : public PtrTable<int, SocketDescriptorBuffer*, mtInternal> {
 public:
  static void init() {
    guarantee(_instance == NULL, "must be");
    _instance = new SocketBufferTable();
    _table_lock = new Monitor(Mutex::leaf, "SocketBufferTable_lock");
  }
  static SocketBufferTable* instance() { return _instance; }

  // Return only complete descriptor records and keep the trailing partial one
  // in the internal fd buffer.
  bool extract_complete_descriptors(int fd, const char* msg, size_t len,
                                    char** descriptors, size_t* descriptors_len);
  void free_buffer(int fd) {
    MutexLocker locker(_table_lock);
    SocketDescriptorBuffer* buffer = PtrTable<int, SocketDescriptorBuffer*, mtInternal>::get(fd);
    if (buffer != NULL) {
      PtrTable<int, SocketDescriptorBuffer*, mtInternal>::remove(fd);
      delete buffer;
    }
  }

 private:
  static SocketBufferTable* _instance;
  static Monitor* _table_lock;
  SocketBufferTable() : PtrTable(NULL) {}
};

class UnreadMsgList : public CHeapObj<mtInternal> {
 public:
  explicit UnreadMsgList(int fd) : _socket_fd(fd) { _head = _tail = new UnreadMsgNode(); }
  ~UnreadMsgList() {
    UnreadMsgNode* cur = _head;
    while (cur != NULL) {
      UnreadMsgNode* tmp = cur;
      cur = cur->next;
      delete tmp;
    }
  }
  void append(void* addr);
  int socket_fd() const { return _socket_fd; }
  int check_timeout();

 private:
  class UnreadMsgNode : public CHeapObj<mtInternal> {
   public:
    explicit UnreadMsgNode(void* addr = NULL, UnreadMsgNode* n = NULL)
        : addr(addr), next(n) {}
    void* addr;
    UnreadMsgNode* next;
  };
  UnreadMsgNode* _head;
  UnreadMsgNode* _tail;
  int _socket_fd;
};

class UnreadMsgTable : public PtrTable<int, UnreadMsgList*, mtInternal> {
 public:
  static void init() {
    guarantee(_instance == NULL, "must be");
    _instance = new UnreadMsgTable();
    _table_lock = new Monitor(Mutex::leaf, "UnreadMsgTable_lock");
  }
  static UnreadMsgTable* instance() { return _instance; }

  void register_fd(int fd) {
    UnreadMsgList* list = new UnreadMsgList(fd);
    MutexLocker locker(_table_lock);
    PtrTable<int, UnreadMsgList*, mtInternal>::add(fd, list);
  }

  void unregister_fd(int fd) {
    MutexLocker locker(_table_lock);
    UnreadMsgList* list = PtrTable<int, UnreadMsgList*, mtInternal>::get(fd);
    if (list != NULL) {
      delete list;
      PtrTable<int, UnreadMsgList*, mtInternal>::remove(fd);
    }
  }

  void add_msg(int fd, void* data_addr) {
    MutexLocker locker(_table_lock);
    UnreadMsgList* list = PtrTable<int, UnreadMsgList*, mtInternal>::get(fd);
    if (list != NULL) {
      list->append(data_addr);
    }
  }

  int check_timeout();
  void start_timer();
  void stop_timer();
  void cleanup();
  static void check_unread_entry(JavaThread* thread, TRAPS);

 private:
  JavaThread* _thread;
  static volatile bool _should_terminate;
  static Monitor* _wait_monitor;
  static Monitor* _table_lock;
  static UnreadMsgTable* _instance;
  UnreadMsgTable() : PtrTable(NULL), _thread(NULL) {}
};

class UBSocketAttachSessionTable : public AllStatic {
 public:
  static bool add(UBSocketAttachSession* session);
  static UBSocketAttachSession* find(const UBSocketEndpoint* local_ep,
                                     const UBSocketEndpoint* remote_ep);
  static void remove(const UBSocketEndpoint* local_ep, const UBSocketEndpoint* remote_ep);
  static void cleanup();
};

class UBSocketEarlyRequestQueue : public AllStatic {
 public:
  static bool cache(int control_fd, const UBSocketControlFrame* request, uint64_t ddl_ns);
  static int count();
  static bool has_requests();
  static bool take_one(int* control_fd, UBSocketControlFrame* request, uint64_t* ddl_ns);
  static void cleanup();
};

#endif  // SHARE_VM_MATRIX_UBSOCKETUTILS_HPP
