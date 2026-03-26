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

#ifndef SHARE_VM_MATRIX_UBSOCKET_HPP
#define SHARE_VM_MATRIX_UBSOCKET_HPP

#include <sys/resource.h>

#include "classfile/symbolTable.hpp"
#include "matrix/matrixUtils.hpp"
#include "runtime/interfaceSupport.hpp"
#include "runtime/javaCalls.hpp"
#include "runtime/thread.hpp"

class UBSocketManager : public AllStatic {
 public:
  static Symbol* shared_memory_name;
  static void* shared_memory_addr;
  static uint64_t package_timeout;

  static void init();
  static void before_exit();

  static void* get_free_memory(long len, long* offset, long* size);
  static long buffer_data(int socket_fd, char* name, long off, long len);
  static long read_data(void* buf, int socket_fd, long len);
  static long write_data(void* buf, int socket_fd, long len);
  static int send_msg(int socket_fd, void* socket_addr, long ub_offset,
                      long len);
  static int send_heartbeat(int socket_fd);
  static long parse_msg(int socket_fd, char* ub_msg_chain);

  static bool register_fd(int socket_fd);
  static bool unregister_fd(int socket_fd);
  static bool has_registered(int socket_fd);

  static void mark(void* addr, uint32_t blk_count) {
    uint64_t nanos = os::javaTimeNanos();
    memcpy(addr, &nanos, sizeof(uint64_t));
    memcpy((char*)addr + sizeof(uint64_t), &blk_count, sizeof(uint32_t));
  };
  static void clean_mark(uintptr_t data_addr) {
    void* addr = reinterpret_cast<char*>(data_addr) - _blk_meta_size;
    memset(addr, 0, sizeof(_blk_meta_size));
  };
  static bool is_marked(const void* addr) {
    uint64_t nanos;
    memcpy(&nanos, addr, sizeof(uint64_t));
    return nanos != 0;
  };

 private:
  static bool _initialized;
  static uint64_t _mem_blk_idx;
  static size_t _blk_size;
  static size_t _blk_meta_size;
  static size_t _blk_count;
  static pthread_mutex_t _mutex;
};

class UBSocketInfoList : public CHeapObj<mtInternal> {
 public:
  // No need for thread safety
  void append(size_t offset, size_t size);  // cache <off,len> for socket data
  size_t read_data(void* dst, size_t len);  // read len data of this fd to dst

  int get_socket_fd() { return _socket_fd; }
  void* get_mem_addr() { return _mem_addr; }
  void set_mem_addr(void* addr) { _mem_addr = addr; }
  Symbol* get_mem_name() { return _mem_name; }
  void set_mem_name(Symbol* name) { _mem_name = name; }

  explicit UBSocketInfoList(int fd)
      : _head(NULL),
        _tail(NULL),
        _cursor(NULL),
        _cur_loc(0),
        _socket_fd(fd),
        _mem_addr(NULL),
        _mem_name(NULL) {}
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

  int delete_nodes(SocketListNode* start, SocketListNode* end);
};

class SocketDataInfoTable
    : public PtrTable<int, UBSocketInfoList*, mtInternal> {
 public:
  static void init() {
    guarantee(_instance == NULL, "must be");
    _instance = new SocketDataInfoTable();
  }
  static SocketDataInfoTable* instance() { return _instance; }
  int unregister_abnormal_fds() {  // unregister fds abnormal exit
    int count = 0;
    begin_iteration();
    UBSocketInfoList* info = next();
    while (info != NULL) {
      UBSocketManager::unregister_fd(info->get_socket_fd());
      count++;
      info = next();
    }
    return count;
  }

 private:
  static SocketDataInfoTable* _instance;
  SocketDataInfoTable() : PtrTable(NULL) {}
};

class SocketNameAddrTable : public PtrTable<Symbol*, void*, mtInternal> {
 public:
  static void init() {
    guarantee(_instance == NULL, "must be");
    _instance = new SocketNameAddrTable();
  }
  static SocketNameAddrTable* instance() { return _instance; }

 private:
  static SocketNameAddrTable* _instance;
  SocketNameAddrTable() : PtrTable(NULL) {}
};

class SocketBufferTable : public PtrTable<int, char*, mtInternal> {
 public:
  static void init() {
    guarantee(_instance == NULL, "must be");
    _instance = new SocketBufferTable();
  }
  static SocketBufferTable* instance() { return _instance; }

  int read_buffer(int fd, char* msg);
  void update_buffer(int fd, char* msg);
  void free_buffer(int fd);

 private:
  static SocketBufferTable* _instance;
  static const size_t DEFAULT_BUFFER_LEN = 1024;
  SocketBufferTable() : PtrTable(NULL), _buffer_len(DEFAULT_BUFFER_LEN) {}

  int _buffer_len;
};

class UnreadMsgList : public CHeapObj<mtInternal> {
 public:
  explicit UnreadMsgList(int fd) : _socket_fd(fd) {
    _head = _tail = new UnreadMsgNode();
    pthread_mutex_init(&_mutex, NULL);
  }
  ~UnreadMsgList() {
    UnreadMsgNode* cur = _head;
    while (cur != NULL) {
      UnreadMsgNode* tmp = cur;
      cur = cur->next;
      delete tmp;
    }
    pthread_mutex_destroy(&_mutex);
  }
  void append(void* addr);
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
  pthread_mutex_t _mutex;
};

class UnreadMsgTable : public PtrTable<int, UnreadMsgList*, mtInternal> {
 public:
  static void init() {
    guarantee(_instance == NULL, "must be");
    _instance = new UnreadMsgTable();
  }
  static UnreadMsgTable* instance() { return _instance; }

  void register_fd(int fd) {
    UnreadMsgList* list = new UnreadMsgList(fd);
    add(fd, list);
  }

  void unregister_fd(int fd) {
    UnreadMsgList* list = get(fd);
    delete list;
    remove(fd);
  }

  void add_msg(int fd, void* data_addr) {
    UnreadMsgList* list = get(fd);
    list->append(data_addr);
  }

  int check_timeout();
  void start_timer();
  void stop_timer();
  static void check_unread_entry(JavaThread* thread, TRAPS);

 private:
  JavaThread* _thread;
  static volatile bool _should_terminate;
  static Monitor* _wait_monitor;
  static UnreadMsgTable* _instance;
  UnreadMsgTable() : PtrTable(NULL), _thread(NULL) {}
};

#endif  // SHARE_VM_MATRIX_UBSOCKET_HPP