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

#ifndef SHARE_VM_MATRIX_UBSOCKETATTACH_HPP
#define SHARE_VM_MATRIX_UBSOCKETATTACH_HPP

#include "matrix/ubSocket/ubSocket.hpp"

struct UBSocketEndpoint;

//   client side                               server side
//     attach_client()                           attach_server()
//     -> ATTACH_REQ ------------------------->  create and publish session
//                                               wait_for_request()
//     <- ATTACH_RSP <-------------------------  publish response
//        acquire and publish remote mapping
//     -> ATTACH_COMMIT ---------------------->  finalize and publish mapping
//     <- ATTACH_ACK <-------------------------  finish session
class UBSocketAttach : public CHeapObj<mtInternal> {
 private:
  int _socket_fd;
  bool _is_server;
  Symbol* _local_mem_name;
  size_t _mem_size;

 public:
  UBSocketAttach(int fd, bool is_server, Symbol* local_mem_name, size_t mem_size);

  bool do_attach();

 private:
  bool attach_client();
  bool attach_server();
  bool publish_server_mapping(const char* client_mem_name);
  bool attach_client_once(int control_fd, const UBSocketEndpoint& request_local_ep,
                          const UBSocketEndpoint& request_remote_ep,
                          uint32_t request_id, uint64_t ddl_ns,
                          char* remote_mem_name, bool* retry);
};

class UBSocketAttachAgent : public AllStatic {
 private:
  static Monitor* _agent_lock;
  static bool _started;
  static bool _exited;
  static int _listen_fd;
  static bool _dual_stack;
  static volatile bool _should_stop;

  static void listener_entry(JavaThread* thread, TRAPS);
  static bool handle_control_connection(int control_fd, bool* keep_open);

 public:
  static void init();
  static bool start();
  static void shutdown();
};

#endif  // SHARE_VM_MATRIX_UBSOCKETATTACH_HPP
