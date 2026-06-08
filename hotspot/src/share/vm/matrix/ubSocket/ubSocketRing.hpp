/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 */

#ifndef SHARE_VM_MATRIX_UBSOCKET_UBSOCKETRING_HPP
#define SHARE_VM_MATRIX_UBSOCKET_UBSOCKETRING_HPP

#include <stddef.h>
#include <stdint.h>

#include "memory/allocation.hpp"
#include "memory/padded.hpp"

enum {
  UB_SOCKET_RING_SLOT_COUNT = 8,
  UB_SOCKET_RING_SLOT_SIZE = 32 * 1024 * 1024,
  UB_SOCKET_RING_INVALID_SLOT = 0xffffffffu
};

struct UBSocketRingOffset {
  uint64_t value;
  DEFINE_PAD_MINUS_SIZE(0, DEFAULT_CACHE_LINE_SIZE, sizeof(uint64_t));
};

struct UBSocketRingHeader {
  UBSocketRingOffset write_offset;
  UBSocketRingOffset peer_read_ack_offset;
};

class UBSocketRing {
 public:
  UBSocketRing() : _header(NULL), _data(NULL), _capacity(0) {}
  explicit UBSocketRing(void* slot_addr) { bind(slot_addr); }

  void bind(void* slot_addr);
  bool valid() const { return _header != NULL && _capacity > 0; }
  bool empty(uint64_t read_offset, bool* valid = NULL) const;

  size_t write(const void* src, size_t len, uint64_t write_offset,
               uint64_t peer_read_ack_offset, uint64_t* new_write_offset);
  size_t read_available(void* dst, size_t len, uint64_t read_offset,
                        uint64_t* new_read_offset);
  uint64_t load_write_offset() const;
  uint64_t load_peer_read_ack_offset() const;
  void publish_write_offset(uint64_t write_offset);
  void publish_peer_read_ack_offset(uint64_t read_ack_offset);

 private:
  UBSocketRingHeader* _header;
  char* _data;
  uint32_t _capacity;

  uint64_t used(uint64_t read_offset, uint64_t write_offset) const {
    return write_offset - read_offset;
  }
  bool window_valid(uint64_t read_offset, uint64_t write_offset) const {
    return write_offset >= read_offset &&
           write_offset - read_offset <= (uint64_t)_capacity;
  }
  uint64_t free_space(uint64_t read_offset, uint64_t write_offset) const {
    return _capacity - used(read_offset, write_offset);
  }
};

class UBSocketRingSlots : public AllStatic {
 public:
  static void init(void* base, size_t size);
  static uint32_t alloc(uint64_t* offset, uint64_t* size);
  static void release(uint32_t slot);
  static void* slot_addr(uint32_t slot);

 private:
  static void* _base;
  static size_t _size;
  static volatile uint32_t _used_mask;
};

#endif // SHARE_VM_MATRIX_UBSOCKET_UBSOCKETRING_HPP
