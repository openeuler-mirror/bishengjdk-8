/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 */

#include "precompiled.hpp"

#include <errno.h>
#include <string.h>

#include "runtime/mutexLocker.hpp"
#include "runtime/orderAccess.inline.hpp"
#include "matrix/ubSocket/ubSocketProfile.hpp"
#include "matrix/ubSocket/ubSocketRing.hpp"

void* UBSocketRingSlots::_mapping = NULL;
UBSocketMemoryLayout UBSocketRingSlots::_layout;
Monitor* UBSocketRingSlots::_lock = NULL;
uint32_t UBSocketRingSlots::_slot_count = 0;
uint8_t* UBSocketRingSlots::_used_slots = NULL;
volatile jint* UBSocketRingSlots::_slot_generations = NULL;
uint32_t* UBSocketRingSlots::_retired_generations = NULL;
uint32_t UBSocketRingSlots::_next_generation = 0;

uint32_t UBSocketMemoryLayout::max_ring_count(uint64_t ring_memory_size) {
  uint64_t minimum_slot_size = sizeof(UBSocketRingWriteState) +
                               sizeof(UBSocketRingPeerState) + 1;
  uint64_t count = ring_memory_size / minimum_slot_size;
  return count > UINT32_MAX ? UINT32_MAX : (uint32_t)count;
}

bool UBSocketMemoryLayout::create(uint64_t ring_memory_size,
                                  uint32_t requested_ring_count,
                                  UBSocketMemoryLayout* layout) {
  if (layout == NULL || requested_ring_count == 0 ||
      requested_ring_count > max_ring_count(ring_memory_size)) {
    return false;
  }
  if (ring_memory_size % requested_ring_count != 0 ||
      sizeof(UBSocketProcessHeader) > UINT64_MAX - ring_memory_size) {
    return false;
  }

  uint64_t ring_unit_size = ring_memory_size / requested_ring_count;
  uint64_t state_size = sizeof(UBSocketRingWriteState) +
                        sizeof(UBSocketRingPeerState);
  uint64_t payload_size = ring_unit_size - state_size;
  if (payload_size > UINT32_MAX) {
    return false;
  }

  layout->ring_count = requested_ring_count;
  layout->write_state_base_offset = sizeof(UBSocketProcessHeader);
  layout->peer_state_base_offset = layout->write_state_base_offset +
      (uint64_t)requested_ring_count * sizeof(UBSocketRingWriteState);
  layout->payload_base_offset = layout->peer_state_base_offset +
      (uint64_t)requested_ring_count * sizeof(UBSocketRingPeerState);
  layout->payload_size = payload_size;
  layout->ring_memory_size = ring_memory_size;
  layout->used_size = sizeof(UBSocketProcessHeader) + ring_memory_size;
  if (layout->used_size > UINT64_MAX - (UB_SOCKET_MEMORY_ALIGNMENT - 1)) {
    return false;
  }
  layout->mapping_size = (layout->used_size + UB_SOCKET_MEMORY_ALIGNMENT - 1) &
                         ~(UB_SOCKET_MEMORY_ALIGNMENT - 1);
  return true;
}

void UBSocketRing::bind(void* write_state_addr, void* peer_state_addr,
                        void* payload_addr, uint64_t payload_size) {
  if (write_state_addr == NULL || peer_state_addr == NULL ||
      payload_addr == NULL || payload_size == 0 || payload_size > UINT32_MAX) {
    _write_state = NULL;
    _peer_state = NULL;
    _data = NULL;
    _capacity = 0;
    return;
  }
  _write_state = reinterpret_cast<UBSocketRingWriteState*>(write_state_addr);
  _peer_state = reinterpret_cast<UBSocketRingPeerState*>(peer_state_addr);
  _data = reinterpret_cast<char*>(payload_addr);
  _capacity = (uint32_t)payload_size;
}

bool UBSocketRing::empty(uint64_t read_offset, bool* valid_window) const {
  if (valid_window != NULL) { *valid_window = true; }
  if (!valid()) { return true; }
  uint64_t write_offset = load_write_offset();
  if (!window_valid(read_offset, write_offset)) {
    errno = EINVAL;
    if (valid_window != NULL) { *valid_window = false; }
    return true;
  }
  return read_offset == write_offset;
}

uint64_t UBSocketRing::load_write_offset() const {
  return valid() ?
      (uint64_t)OrderAccess::load_acquire(
          (volatile julong*)&_write_state->write_offset) : 0;
}

uint64_t UBSocketRing::load_peer_read_ack_offset() const {
  if (!valid()) { return 0; }
  return (uint64_t)OrderAccess::load_acquire(
      (volatile julong*)&_peer_state->read_ack_offset);
}

uint32_t UBSocketRing::load_peer_quiesced_generation() const {
  if (!valid()) { return 0; }
  return (uint32_t)OrderAccess::load_acquire(
      (volatile jint*)&_peer_state->quiesced_generation);
}

void UBSocketRing::publish_write_offset(uint64_t write_offset) {
  if (!valid()) { return; }
  OrderAccess::release_store((volatile julong*)&_write_state->write_offset,
                             (julong)write_offset);
}

void UBSocketRing::publish_peer_read_ack_offset(uint64_t read_ack_offset) {
  if (!valid()) { return; }
  OrderAccess::release_store((volatile julong*)&_peer_state->read_ack_offset,
                             (julong)read_ack_offset);
}

void UBSocketRing::publish_peer_quiesced_generation(uint32_t generation) {
  if (!valid()) { return; }
  OrderAccess::release_store(
      (volatile jint*)&_peer_state->quiesced_generation,
      (jint)generation);
}

void UBSocketRing::reset_for_allocation() {
  publish_write_offset(0);
  publish_peer_read_ack_offset(0);
  publish_peer_quiesced_generation(0);
}

size_t UBSocketRing::write(const void* src, size_t len, uint64_t write_offset,
                           uint64_t peer_read_ack_offset,
                           uint64_t* new_write_offset) {
  *new_write_offset = write_offset;
  if (!valid() || src == NULL || len == 0) { return 0; }
  if (!window_valid(peer_read_ack_offset, write_offset)) {
    errno = EINVAL;
    return (size_t)-1;
  }
  uint64_t used_before = used(peer_read_ack_offset, write_offset);
  uint64_t free_bytes = free_space(peer_read_ack_offset, write_offset);
  if (free_bytes == 0) {
    UBSocketProfiler::max_bytes(UB_PROF_RING_WRITE_MAX_USED_BYTES, used_before);
    UBSocketProfiler::count(UB_PROF_RING_WRITE_FULL, len);
    return 0;
  }

  size_t ncopy = len < free_bytes ? len : free_bytes;
  UBSocketProfiler::max_bytes(UB_PROF_RING_WRITE_MAX_USED_BYTES,
                              used_before + (uint64_t)ncopy);
  if (ncopy < len) {
    UBSocketProfiler::count(UB_PROF_RING_WRITE_PARTIAL, len - ncopy);
  }
  UBSocketProfileScope total_profile(UB_PROF_RING_WRITE_TOTAL, ncopy);
  uint32_t pos = (uint32_t)(write_offset % _capacity);
  uint32_t first = (uint32_t)ncopy;
  if (first > _capacity - pos) { first = _capacity - pos; }
  {
    UBSocketProfileScope memcpy_profile(UB_PROF_RING_WRITE_MEMCPY, ncopy);
    memcpy(_data + pos, src, first);
    if (first < ncopy) {
      memcpy(_data, reinterpret_cast<const char*>(src) + first, ncopy - first);
    }
  }

  *new_write_offset = write_offset + (uint64_t)ncopy;
  publish_write_offset(*new_write_offset);
  return ncopy;
}

size_t UBSocketRing::read_available(void* dst, size_t len, uint64_t read_offset,
                                    uint64_t* new_read_offset) {
  *new_read_offset = read_offset;
  if (!valid() || dst == NULL || len == 0) { return 0; }
  uint64_t write_offset = load_write_offset();
  if (!window_valid(read_offset, write_offset)) {
    errno = EINVAL;
    return (size_t)-1;
  }
  uint64_t avail = used(read_offset, write_offset);
  if (avail == 0) { return 0; }
  UBSocketProfiler::max_bytes(UB_PROF_RING_READ_MAX_USED_BYTES, avail);

  size_t ncopy = len < avail ? len : avail;
  UBSocketProfileScope total_profile(UB_PROF_RING_READ_TOTAL, ncopy);
  uint32_t pos = (uint32_t)(read_offset % _capacity);
  uint32_t first = (uint32_t)ncopy;
  if (first > _capacity - pos) { first = _capacity - pos; }
  uint64_t memcpy_start = UBSocketProfiler::start(UB_PROF_RING_READ_MEMCPY);
  memcpy(dst, _data + pos, first);
  if (first < ncopy) {
    memcpy(reinterpret_cast<char*>(dst) + first, _data, ncopy - first);
  }
  UBSocketProfiler::end(UB_PROF_RING_READ_MEMCPY, memcpy_start, ncopy);
  *new_read_offset = read_offset + (uint64_t)ncopy;
  return ncopy;
}

bool UBSocketRingSlots::init(void* mapping,
                             const UBSocketMemoryLayout& layout) {
  if (_lock == NULL) {
    _lock = new Monitor(Mutex::leaf, "UBSocketRingSlots_lock");
  }
  if (_used_slots != NULL) {
    FREE_C_HEAP_ARRAY(uint8_t, _used_slots, mtInternal);
    _used_slots = NULL;
  }
  if (_slot_generations != NULL) {
    FREE_C_HEAP_ARRAY(jint, _slot_generations, mtInternal);
    _slot_generations = NULL;
  }
  if (_retired_generations != NULL) {
    FREE_C_HEAP_ARRAY(uint32_t, _retired_generations, mtInternal);
    _retired_generations = NULL;
  }
  _mapping = mapping;
  _layout = layout;
  _slot_count = layout.ring_count;
  _next_generation = 0;
  if (_mapping == NULL || _slot_count == 0 || layout.payload_size == 0 ||
      layout.mapping_size <= layout.payload_base_offset) {
    return false;
  }
  _used_slots = NEW_C_HEAP_ARRAY(uint8_t, _slot_count, mtInternal);
  _slot_generations = NEW_C_HEAP_ARRAY(jint, _slot_count, mtInternal);
  _retired_generations =
      NEW_C_HEAP_ARRAY(uint32_t, _slot_count, mtInternal);
  memset(_used_slots, 0, _slot_count);
  memset(const_cast<jint*>(_slot_generations), 0,
         (size_t)_slot_count * sizeof(jint));
  memset(_retired_generations, 0,
         (size_t)_slot_count * sizeof(uint32_t));
  return true;
}

void UBSocketRingSlots::reclaim_retired_locked() {
  for (uint32_t slot = 0; slot < _slot_count; slot++) {
    uint32_t generation = _retired_generations[slot];
    if (generation == 0) { continue; }
    UBSocketRing ring(write_state(slot), peer_state(slot),
                      payload_addr(slot), _layout.payload_size);
    if (ring.load_peer_quiesced_generation() == generation) {
      _retired_generations[slot] = 0;
      _used_slots[slot] = 0;
    }
  }
}

uint32_t UBSocketRingSlots::alloc(uint64_t* offset, uint64_t* size, uint32_t* generation) {
  if (_mapping == NULL || _used_slots == NULL) {
    return UB_SOCKET_RING_INVALID_SLOT;
  }
  MonitorLockerEx locker(_lock, Mutex::_no_safepoint_check_flag);
  reclaim_retired_locked();
  for (uint32_t slot = 0; slot < _slot_count; slot++) {
    if (_used_slots[slot] == 0) {
      _used_slots[slot] = 1;
      UBSocketRing ring(write_state(slot), peer_state(slot),
                        payload_addr(slot), _layout.payload_size);
      ring.reset_for_allocation();
      _next_generation++;
      if (_next_generation == 0) { _next_generation = 1; }
      OrderAccess::release_store(_slot_generations + slot,
                                 (jint)_next_generation);
      *offset = _layout.payload_offset(slot);
      *size = _layout.payload_size;
      *generation = _next_generation;
      return slot;
    }
  }
  return UB_SOCKET_RING_INVALID_SLOT;
}

void UBSocketRingSlots::release(uint32_t slot) {
  if (_used_slots == NULL || slot >= _slot_count) { return; }
  MonitorLockerEx locker(_lock, Mutex::_no_safepoint_check_flag);
  _retired_generations[slot] = 0;
  _used_slots[slot] = 0;
}

void UBSocketRingSlots::retire(uint32_t slot, uint32_t generation) {
  if (_used_slots == NULL || _retired_generations == NULL ||
      slot >= _slot_count || generation == 0) {
    return;
  }
  MonitorLockerEx locker(_lock, Mutex::_no_safepoint_check_flag);
  _retired_generations[slot] = generation;
}

UBSocketRingWriteState* UBSocketRingSlots::write_state(uint32_t slot) {
  return reinterpret_cast<UBSocketRingWriteState*>(
      reinterpret_cast<char*>(_mapping) + _layout.write_state_offset(slot));
}

UBSocketRingPeerState* UBSocketRingSlots::peer_state(uint32_t slot) {
  return reinterpret_cast<UBSocketRingPeerState*>(
      reinterpret_cast<char*>(_mapping) + _layout.peer_state_offset(slot));
}

void* UBSocketRingSlots::payload_addr(uint32_t slot) {
  return reinterpret_cast<char*>(_mapping) + _layout.payload_offset(slot);
}

uint32_t UBSocketRingSlots::slot_generation(uint32_t slot) {
  if (_slot_generations == NULL || slot >= _slot_count) { return 0; }
  return (uint32_t)OrderAccess::load_acquire(_slot_generations + slot);
}
