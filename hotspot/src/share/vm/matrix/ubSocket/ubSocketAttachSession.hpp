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

#ifndef SHARE_VM_MATRIX_UBSOCKETATTACHSSESSION_HPP
#define SHARE_VM_MATRIX_UBSOCKETATTACHSSESSION_HPP

#include "matrix/ubSocket/ubSocketFrame.hpp"
#include "memory/allocation.hpp"

enum UBSocketAttachPhase {
  ATTACH_REQUESTED,       // Waiting for the control side to deliver ATTACH_REQ.
  ATTACH_PREPARED,        // ATTACH_REQ accepted; the data side is preparing the response.
  ATTACH_PREPARE_FAILED,  // The data side failed before ATTACH_RSP could report success.
  ATTACH_COMMITTED,       // ATTACH_RSP reported success; waiting ATTACH_COMMIT from control side.
  ATTACH_COMMIT_FAILED,   // ATTACH_COMMIT reported failure.
  ATTACH_FINALIZED,       // ATTACH_COMMIT reported success; waiting data side's final result.
  ATTACH_PENDING_SUCCESS, // The data side finalized the attach as success.
  ATTACH_PENDING_FAILURE, // The data side finalized the attach as failure.
  ATTACH_DONE,            // The control side received ATTACH_ACK and finished the session.
  ATTACH_ABORTED          // The session is aborted due to any failure in the handshake.
};

// Server-side attach rendezvous for one accepted data socket.
//
// A server-side UBSocket attach is driven by two independent paths:
//   - the data path creates and publishes this session from attach_server();
//   - the control path receives ATTACH_REQ on the attach agent thread and
//     finds the session from UBSocketSessionCaches.
//
// The two paths can arrive in either order. If the control request arrives
// first it is kept in UBSocketEarlyReqQueue until the data path publishes the
// matching session. Once matched, this object is the per-fd state machine and
// monitor used to exchange memory names, publish the remote mapping result,
// and decide whether the fd is bound to UBSocket or falls back to TCP.
//
//   control side                         data side
//   ------------                         ---------
//   ATTACH_REQ arrives                   session created and published
//   accept_request()                     wait_for_request()
//   wait_for_response()                  prepare remote mapping
//   send ATTACH_RSP        <──────────   publish_response()
//   recv ATTACH_COMMIT
//   accept_commit()                      wait_for_commit()
//   wait_for_final_result()              bind/finalize attach result
//   send ATTACH_ACK         <──────────   finish_pending()
//   finish_control()
//
// UBSocketSessionCaches::find() pins a session before returning it to the
// attach agent thread. remove()/cleanup() mark the session closing and wait for
// active pins to drain before deleting it, so a control thread cannot use a
// session after the data path removes it from the cache.
class UBSocketAttachSession : public CHeapObj<mtInternal> {
 private:
  Monitor* _monitor;  // wait/notify between threads during handshake
  UBSocketEndpoint _local_endpoint;
  UBSocketEndpoint _remote_endpoint;
  uint32_t _request_id;
  char _local_mem_name[UB_SOCKET_MEM_NAME_BUF_LEN];
  char _client_mem_name[UB_SOCKET_MEM_NAME_BUF_LEN];
  UBSocketAttachPhase _phase;
  UBSocketAttachSession* _next;
  bool _closing;
  int _active_count;

  bool wait_until(uint64_t ddl_ns);

  void accept_request(const char* client_mem_name, uint32_t request_id);
  bool wait_for_response(uint64_t ddl_ns);
  void accept_commit(bool success);
  bool wait_for_final_result(uint64_t ddl_ns);
  void finish_control(bool success);

  void publish_response(bool success);
  bool wait_for_commit(uint64_t ddl_ns);
  bool finish_pending(bool success, uint64_t ddl_ns);

 public:
  UBSocketAttachSession(const UBSocketEndpoint* local_ep, const UBSocketEndpoint* remote_ep,
                        const char* local_mem_name);
  ~UBSocketAttachSession();

  const UBSocketEndpoint& local_endpoint() const { return _local_endpoint; }
  const UBSocketEndpoint& remote_endpoint() const { return _remote_endpoint; }
  uint32_t request_id() const { return _request_id; }
  const char* local_mem_name() const { return _local_mem_name; }

  bool is_prepared() const { return _phase == ATTACH_COMMITTED; }
  bool is_finalized_ok() const { return _phase == ATTACH_PENDING_SUCCESS; }
  bool failed() const { return _phase == ATTACH_ABORTED; }
  bool done() const { return _phase == ATTACH_DONE; }

  UBSocketAttachSession* next() const { return _next; }
  void set_next(UBSocketAttachSession* next) { _next = next; }
  // Prevent cache remove/cleanup from deleting a session in use by agent thread.
  bool try_pin();
  void unpin();
  void close_and_wait();

  bool matches(const UBSocketEndpoint* local_ep, const UBSocketEndpoint* remote_ep) const;

  bool wait_for_request(uint64_t ddl_ns, char* client_mem_name);
  // Control side entry: drive ATTACH_REQ/RSP/COMMIT/ACK on control_fd.
  bool drive_server_handshake(int control_fd,
                              const UBSocketAttachFrame* request,
                              uint64_t ddl_ns);
  bool finish_server_attach(bool prepare_success, uint64_t ddl_ns);
};

#endif  // SHARE_VM_MATRIX_UBSOCKETATTACHSSESSION_HPP
