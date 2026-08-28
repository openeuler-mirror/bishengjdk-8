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
static const uint64_t UB_SOCKET_MEMORY_ALIGNMENT = 4 * M;

// Sender-written process notification state occupies one cache line. The local
// Receiver only acquire-loads this header after initialization.
struct UBSocketProcessHeader {
  uint32_t version;
  uint32_t capabilities;
  uint32_t ring_count;
  uint32_t reserved;
  uint64_t mapping_size;
  uint64_t ring_memory_size;
  volatile jint notify_generation;
  DEFINE_PAD_MINUS_SIZE(0, DEFAULT_CACHE_LINE_SIZE,
                        sizeof(uint32_t) * 4 + sizeof(uint64_t) * 2 +
                        sizeof(jint));
};

struct UBSocketRingWriteState {
  uint64_t write_offset;
  DEFINE_PAD_MINUS_SIZE(0, DEFAULT_CACHE_LINE_SIZE, sizeof(uint64_t));
};

struct UBSocketRingPeerState {
  uint64_t read_ack_offset;
  uint32_t quiesced_generation;
  DEFINE_PAD_MINUS_SIZE(0, DEFAULT_CACHE_LINE_SIZE,
                        sizeof(uint64_t) + sizeof(uint32_t));
};

struct UBSocketMemoryLayout {
  uint32_t ring_count;
  uint64_t write_state_base_offset;
  uint64_t peer_state_base_offset;
  uint64_t payload_base_offset;
  uint64_t payload_size;
  uint64_t ring_memory_size;
  uint64_t used_size;
  uint64_t mapping_size;

  static bool create(uint64_t ring_memory_size, uint32_t ring_count,
                     UBSocketMemoryLayout* layout);
  static uint32_t max_ring_count(uint64_t ring_memory_size);
  uint64_t write_state_offset(uint32_t slot) const {
    return write_state_base_offset +
           (uint64_t)slot * sizeof(UBSocketRingWriteState);
  }
  uint64_t peer_state_offset(uint32_t slot) const {
    return peer_state_base_offset +
           (uint64_t)slot * sizeof(UBSocketRingPeerState);
  }
  uint64_t payload_offset(uint32_t slot) const {
    return payload_base_offset + (uint64_t)slot * payload_size;
  }
};

class UBSocketRing {
 public:
  UBSocketRing()
      : _write_state(NULL), _peer_state(NULL), _data(NULL), _capacity(0) {}
  UBSocketRing(void* write_state_addr, void* peer_state_addr,
               void* payload_addr, uint64_t payload_size) {
    bind(write_state_addr, peer_state_addr, payload_addr, payload_size);
  }

  void bind(void* write_state_addr, void* peer_state_addr,
            void* payload_addr, uint64_t payload_size);
  bool valid() const {
    return _write_state != NULL && _peer_state != NULL &&
           _data != NULL && _capacity > 0;
  }
  uint64_t capacity() const { return (uint64_t)_capacity; }
  bool empty(uint64_t read_offset, bool* valid = NULL) const;

  size_t write(const void* src, size_t len, uint64_t write_offset,
               uint64_t peer_read_ack_offset, uint64_t* new_write_offset);
  size_t read_available(void* dst, size_t len, uint64_t read_offset,
                        uint64_t* new_read_offset);
  uint64_t load_write_offset() const;
  uint64_t load_peer_read_ack_offset() const;
  uint32_t load_peer_quiesced_generation() const;
  void publish_write_offset(uint64_t write_offset);
  void publish_peer_read_ack_offset(uint64_t read_ack_offset);
  void publish_peer_quiesced_generation(uint32_t generation);
  void reset_for_allocation();

 private:
  UBSocketRingWriteState* _write_state;
  UBSocketRingPeerState* _peer_state;
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
  static bool init(void* mapping, const UBSocketMemoryLayout& layout);
  static uint32_t alloc(uint64_t* offset, uint64_t* size,
                        uint32_t* generation);
  static void release(uint32_t slot);
  static void retire(uint32_t slot, uint32_t generation);
  static UBSocketRingWriteState* write_state(uint32_t slot);
  static UBSocketRingPeerState* peer_state(uint32_t slot);
  static void* payload_addr(uint32_t slot);
  static uint32_t slot_count() { return _slot_count; }
  static uint32_t slot_generation(uint32_t slot);
  static uint64_t payload_size() { return _layout.payload_size; }
  static uint64_t write_state_mapping_offset(uint32_t slot) {
    return _layout.write_state_offset(slot);
  }
  static uint64_t peer_state_mapping_offset(uint32_t slot) {
    return _layout.peer_state_offset(slot);
  }
  static uint64_t payload_mapping_offset(uint32_t slot) {
    return _layout.payload_offset(slot);
  }

 private:
  static void* _mapping;
  static UBSocketMemoryLayout _layout;
  static Monitor* _lock;
  static uint32_t _slot_count;
  static uint8_t* _used_slots;
  static volatile jint* _slot_generations;
  static uint32_t* _retired_generations;
  static uint32_t _next_generation;

  static void reclaim_retired_locked();
};

#endif // SHARE_VM_MATRIX_UBSOCKET_UBSOCKETRING_HPP
