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

class Monitor;

enum {
  UB_SOCKET_RING_INVALID_SLOT = UINT32_MAX
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
  UBSocketRing(void* slot_addr, uint64_t slot_size) { bind(slot_addr, slot_size); }

  void bind(void* slot_addr, uint64_t slot_size);
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
  static bool init(void* base, size_t size, uint32_t slot_count);
  static uint32_t alloc(uint64_t* offset, uint64_t* size);
  static void release(uint32_t slot);
  static void* slot_addr(uint32_t slot);
  static uint32_t slot_count() { return _slot_count; }
  static uint64_t slot_size() { return _slot_size; }

 private:
  static void* _base;
  static size_t _size;
  static Monitor* _lock;
  static uint32_t _slot_count;
  static uint64_t _slot_size;
  static uint8_t* _used_slots;
};

#endif // SHARE_VM_MATRIX_UBSOCKET_UBSOCKETRING_HPP
