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

#include "runtime/atomic.hpp"
#include "runtime/orderAccess.inline.hpp"
#include "matrix/ubSocket/ubSocketProfile.hpp"
#include "matrix/ubSocket/ubSocketRing.hpp"

void* UBSocketRingSlots::_base = NULL;
size_t UBSocketRingSlots::_size = 0;
volatile uint32_t UBSocketRingSlots::_used_mask = 0;

void UBSocketRing::bind(void* slot_addr) {
  _header = reinterpret_cast<UBSocketRingHeader*>(slot_addr);
  _data = reinterpret_cast<char*>(slot_addr) + sizeof(UBSocketRingHeader);
  _capacity = UB_SOCKET_RING_SLOT_SIZE - sizeof(UBSocketRingHeader);
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
  uint64_t free_bytes = free_space(peer_read_ack_offset, write_offset);
  if (free_bytes == 0) {
    UBSocketProfiler::count(UB_PROF_RING_WRITE_FULL, len);
    return 0;
  }

  size_t ncopy = len < free_bytes ? len : free_bytes;
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

void UBSocketRingSlots::init(void* base, size_t size) {
  _base = base;
  _size = size;
  _used_mask = 0;
}

uint32_t UBSocketRingSlots::alloc(uint64_t* offset, uint64_t* size) {
  if (_base == NULL || _size < (size_t)UB_SOCKET_RING_SLOT_COUNT * UB_SOCKET_RING_SLOT_SIZE) {
    return UB_SOCKET_RING_INVALID_SLOT;
  }
  for (uint32_t slot = 0; slot < UB_SOCKET_RING_SLOT_COUNT; slot++) {
    uint32_t bit = 1u << slot;
    uint32_t old_mask = _used_mask;
    while ((old_mask & bit) == 0) {
      uint32_t new_mask = old_mask | bit;
      uint32_t observed = (uint32_t)Atomic::cmpxchg((jint)new_mask,
          (volatile jint*)&_used_mask, (jint)old_mask);
      if (observed == old_mask) {
        void* addr = slot_addr(slot);
        memset(addr, 0, UB_SOCKET_RING_SLOT_SIZE);
        *offset = (uint64_t)slot * UB_SOCKET_RING_SLOT_SIZE;
        *size = UB_SOCKET_RING_SLOT_SIZE;
        return slot;
      }
      old_mask = observed;
    }
  }
  return UB_SOCKET_RING_INVALID_SLOT;
}

void UBSocketRingSlots::release(uint32_t slot) {
  if (slot >= UB_SOCKET_RING_SLOT_COUNT) { return; }
  uint32_t bit = 1u << slot;
  uint32_t old_mask = _used_mask;
  while ((old_mask & bit) != 0) {
    uint32_t new_mask = old_mask & ~bit;
    uint32_t observed = (uint32_t)Atomic::cmpxchg((jint)new_mask,
        (volatile jint*)&_used_mask, (jint)old_mask);
    if (observed == old_mask) { return; }
    old_mask = observed;
  }
}

void* UBSocketRingSlots::slot_addr(uint32_t slot) {
  return reinterpret_cast<char*>(_base) + (size_t)slot * UB_SOCKET_RING_SLOT_SIZE;
}
