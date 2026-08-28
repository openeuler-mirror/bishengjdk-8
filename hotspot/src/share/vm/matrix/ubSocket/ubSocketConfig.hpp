/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES FROM THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 */

#ifndef SHARE_VM_MATRIX_UBSOCKET_UBSOCKETCONFIG_HPP
#define SHARE_VM_MATRIX_UBSOCKET_UBSOCKETCONFIG_HPP

#include <stddef.h>

#include "memory/allocation.hpp"

class outputStream;
struct UBSocketEndpoint;

// One immutable UBSocket.conf generation. Instances are published and
// reclaimed by UBSocketManager after all registration readers leave.
class UBSocketConfig : public CHeapObj<mtInternal> {
 private:
  struct StackEntry;
  struct SelfPortEntry;
  struct RemoteMappingEntry;

  char* _source_path;
  StackEntry* _stack_head;
  StackEntry* _stack_tail;
  SelfPortEntry* _self_port_head;
  SelfPortEntry* _self_port_tail;
  RemoteMappingEntry* _remote_head;
  RemoteMappingEntry* _remote_tail;
  UBSocketEndpoint* _self_public_ip;
  UBSocketEndpoint* _self_local_ip;
  bool _self_local_ip_wildcard;
  bool _has_self_ip;
  bool _has_self_mapping;
  int _stack_count;
  int _self_port_count;
  int _remote_mapping_count;

  explicit UBSocketConfig(const char* source_path);

 public:
  ~UBSocketConfig();

  // strict=false preserves startup compatibility by warning and ignoring bad
  // entries. strict=true rejects the complete candidate used by jcmd reload.
  static UBSocketConfig* load(const char* path, bool strict,
                              char* error, size_t error_len);

  bool check_stack() const;
  bool control_endpoint_for_data(const UBSocketEndpoint* data_ep,
                                 UBSocketEndpoint* control_ep) const;
  bool matches_local_service(const UBSocketEndpoint* local_ep) const;
  bool has_mapping_for_data(const UBSocketEndpoint* data_ep) const;
  bool matches_local_data(const UBSocketEndpoint* request_ep,
                          const UBSocketEndpoint* session_ep) const;
  void print_on(outputStream* out) const;

  int stack_count() const { return _stack_count; }
  int self_port_count() const { return _self_port_count; }
  int remote_mapping_count() const { return _remote_mapping_count; }
  const char* source_path() const { return _source_path; }
};

#endif  // SHARE_VM_MATRIX_UBSOCKET_UBSOCKETCONFIG_HPP
