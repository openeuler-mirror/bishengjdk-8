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

void* UBSocketRingSlots::_base = NULL;
size_t UBSocketRingSlots::_size = 0;
Monitor* UBSocketRingSlots::_lock = NULL;
uint32_t UBSocketRingSlots::_slot_count = 0;
uint64_t UBSocketRingSlots::_slot_size = 0;
uint8_t* UBSocketRingSlots::_used_slots = NULL;

void UBSocketRing::bind(void* slot_addr, uint64_t slot_size) {
  if (slot_addr == NULL || slot_size <= sizeof(UBSocketRingHeader) ||
      slot_size > (uint64_t)UINT32_MAX) {
    _header = NULL;
    _data = NULL;
    _capacity = 0;
    return;
  }
  _header = reinterpret_cast<UBSocketRingHeader*>(slot_addr);
  _data = reinterpret_cast<char*>(slot_addr) + sizeof(UBSocketRingHeader);
  _capacity = (uint32_t)(slot_size - sizeof(UBSocketRingHeader));
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
          (volatile julong*)&_header->write_offset.value) : 0;
}

uint64_t UBSocketRing::load_peer_read_ack_offset() const {
  if (!valid()) { return 0; }
  return (uint64_t)OrderAccess::load_acquire(
      (volatile julong*)&_header->peer_read_ack_offset.value);
}

void UBSocketRing::publish_write_offset(uint64_t write_offset) {
  if (!valid()) { return; }
  OrderAccess::release_store((volatile julong*)&_header->write_offset.value,
                             (julong)write_offset);
}

void UBSocketRing::publish_peer_read_ack_offset(uint64_t read_ack_offset) {
  if (!valid()) { return; }
  OrderAccess::release_store((volatile julong*)&_header->peer_read_ack_offset.value,
                             (julong)read_ack_offset);
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

bool UBSocketRingSlots::init(void* base, size_t size, uint32_t slot_count) {
  if (_lock == NULL) {
    _lock = new Monitor(Mutex::leaf, "UBSocketRingSlots_lock");
  }
  if (_used_slots != NULL) {
    FREE_C_HEAP_ARRAY(uint8_t, _used_slots, mtInternal);
    _used_slots = NULL;
  }
  _base = base;
  _size = size;
  _slot_count = slot_count;
  _slot_size = slot_count == 0 ? 0 : (uint64_t)size / slot_count;
  if (_base == NULL || _slot_count == 0 ||
      _slot_size <= sizeof(UBSocketRingHeader) ||
      _slot_size > (uint64_t)UINT32_MAX) {
    return false;
  }
  _used_slots = NEW_C_HEAP_ARRAY(uint8_t, _slot_count, mtInternal);
  memset(_used_slots, 0, _slot_count);
  return true;
}

uint32_t UBSocketRingSlots::alloc(uint64_t* offset, uint64_t* size) {
  if (_base == NULL || _used_slots == NULL) {
    return UB_SOCKET_RING_INVALID_SLOT;
  }
  MonitorLockerEx locker(_lock, Mutex::_no_safepoint_check_flag);
  for (uint32_t slot = 0; slot < _slot_count; slot++) {
    if (_used_slots[slot] == 0) {
      _used_slots[slot] = 1;
      void* addr = slot_addr(slot);
      memset(addr, 0, (size_t)_slot_size);
      *offset = (uint64_t)slot * _slot_size;
      *size = _slot_size;
      return slot;
    }
  }
  return UB_SOCKET_RING_INVALID_SLOT;
}

void UBSocketRingSlots::release(uint32_t slot) {
  if (_used_slots == NULL || slot >= _slot_count) { return; }
  MonitorLockerEx locker(_lock, Mutex::_no_safepoint_check_flag);
  _used_slots[slot] = 0;
}

void* UBSocketRingSlots::slot_addr(uint32_t slot) {
  return reinterpret_cast<char*>(_base) + (uint64_t)slot * _slot_size;
}
