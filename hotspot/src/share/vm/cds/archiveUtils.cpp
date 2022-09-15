/*
 * Copyright (c) 2019, 2021, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2022, Huawei Technologies Co., Ltd. All rights reserved.
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
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#include "precompiled.hpp"
#include "cds/archiveBuilder.hpp"
#include "cds/archiveUtils.hpp"
#include "cds/dynamicArchive.hpp"
#include "classfile/systemDictionaryShared.hpp"
#include "memory/filemap.hpp"
#include "memory/resourceArea.hpp"
#include "runtime/arguments.hpp"
#include "utilities/bitMap.inline.hpp"
#include "utilities/align.hpp"

BitMap* ArchivePtrMarker::_ptrmap = NULL;
VirtualSpace* ArchivePtrMarker::_vs;

bool ArchivePtrMarker::_compacted;

void ArchivePtrMarker::initialize(BitMap* ptrmap, VirtualSpace* vs) {
  assert(_ptrmap == NULL, "initialize only once");
  _vs = vs;
  _compacted = false;
  _ptrmap = ptrmap;

  // Use this as initial guesstimate. We should need less space in the
  // archive, but if we're wrong the bitmap will be expanded automatically.
  size_t estimated_archive_size = MetaspaceGC::capacity_until_GC();
  // But set it smaller in debug builds so we always test the expansion code.
  // (Default archive is about 12MB).
  DEBUG_ONLY(estimated_archive_size = 6 * M);

  // We need one bit per pointer in the archive.
  _ptrmap->resize(estimated_archive_size / sizeof(intptr_t), false);
}

void ArchivePtrMarker::mark_pointer(address* ptr_loc) {
  assert(_ptrmap != NULL, "not initialized");
  assert(!_compacted, "cannot mark anymore");

  if (ptr_base() <= ptr_loc && ptr_loc < ptr_end()) {
    address value = *ptr_loc;
    // We don't want any pointer that points to very bottom of the archive, otherwise when
    // MetaspaceShared::default_base_address()==0, we can't distinguish between a pointer
    // to nothing (NULL) vs a pointer to an objects that happens to be at the very bottom
    // of the archive.
    assert(value != (address)ptr_base(), "don't point to the bottom of the archive");

    if (value != NULL) {
      assert(uintx(ptr_loc) % sizeof(intptr_t) == 0, "pointers must be stored in aligned addresses");
      size_t idx = ptr_loc - ptr_base();
      if (_ptrmap->size() <= idx) {
        _ptrmap->resize((idx + 1) * 2, false);
      }
      assert(idx < _ptrmap->size(), "must be");
      _ptrmap->set_bit(idx);
      if (TraceDynamicCDS) {
        dynamic_cds_log->print_cr("Marking pointer [" PTR_FORMAT "] -> " PTR_FORMAT " @ " SIZE_FORMAT_W(5), p2i(ptr_loc), p2i(*ptr_loc), idx);
      }
    }
  }
}

void ArchivePtrMarker::clear_pointer(address* ptr_loc) {
  assert(_ptrmap != NULL, "not initialized");
  assert(!_compacted, "cannot clear anymore");

  assert(ptr_base() <= ptr_loc && ptr_loc < ptr_end(), "must be");
  assert(uintx(ptr_loc) % sizeof(intptr_t) == 0, "pointers must be stored in aligned addresses");
  size_t idx = ptr_loc - ptr_base();
  assert(idx < _ptrmap->size(), "cannot clear pointers that have not been marked");
  _ptrmap->clear_bit(idx);
  if (TraceDynamicCDS)
  dynamic_cds_log->print_cr("Clearing pointer [" PTR_FORMAT "] -> " PTR_FORMAT " @ " SIZE_FORMAT_W(5), p2i(ptr_loc), p2i(*ptr_loc), idx);
}

class ArchivePtrBitmapCleaner: public BitMapClosure {
  BitMap* _ptrmap;
  address* _ptr_base;
  address  _relocatable_base;
  address  _relocatable_end;
  size_t   _max_non_null_offset;

public:
  ArchivePtrBitmapCleaner(BitMap* ptrmap, address* ptr_base, address relocatable_base, address relocatable_end) :
    _ptrmap(ptrmap), _ptr_base(ptr_base),
    _relocatable_base(relocatable_base), _relocatable_end(relocatable_end), _max_non_null_offset(0) {}

  bool do_bit(size_t offset) {
    address* ptr_loc = _ptr_base + offset;
    address  ptr_value = *ptr_loc;
    if (ptr_value != NULL) {
      assert(_relocatable_base <= ptr_value && ptr_value < _relocatable_end, "do not point to arbitrary locations!");
      if (_max_non_null_offset < offset) {
        _max_non_null_offset = offset;
      }
    } else {
      _ptrmap->clear_bit(offset);
    }

    return true;
  }

  size_t max_non_null_offset() const { return _max_non_null_offset; }
};

void ArchivePtrMarker::compact(address relocatable_base, address relocatable_end) {
  assert(!_compacted, "cannot compact again");
  ArchivePtrBitmapCleaner cleaner(_ptrmap, ptr_base(), relocatable_base, relocatable_end);
  _ptrmap->iterate(&cleaner);
  compact(cleaner.max_non_null_offset());
}

void ArchivePtrMarker::compact(size_t max_non_null_offset) {
  assert(!_compacted, "cannot compact again");
  _ptrmap->resize(max_non_null_offset + 1, false);
  _compacted = true;
}

char* DumpRegion::expand_top_to(char* newtop) {
  assert(is_allocatable(), "must be initialized and not packed");
  assert(newtop >= _top, "must not grow backwards");
  if (newtop > _end) {
    vm_exit_during_initialization("Unable to allocate memory",
                                  "Please reduce the number of shared classes.");
    ShouldNotReachHere();
  }

  commit_to(newtop);
  _top = newtop;

  if (_max_delta > 0) {
    uintx delta = ArchiveBuilder::current()->buffer_to_offset((address)(newtop-1));
    if (delta > _max_delta) {
      // This is just a sanity check and should not appear in any real world usage. This
      // happens only if you allocate more than 2GB of shared objects and would require
      // millions of shared classes.
      vm_exit_during_initialization("Out of memory in the CDS archive",
                                    "Please reduce the number of shared classes.");
    }
  }

  return _top;
}

void DumpRegion::commit_to(char* newtop) {
  Arguments::assert_is_dumping_archive();
  char* base = _rs->base();
  size_t need_committed_size = newtop - base;
  size_t has_committed_size = _vs->committed_size();
  if (need_committed_size < has_committed_size) {
    return;
  }

  size_t min_bytes = need_committed_size - has_committed_size;
  size_t preferred_bytes = 1 * M;
  size_t uncommitted = _vs->reserved_size() - has_committed_size;

  size_t commit = MAX2(min_bytes, preferred_bytes);
  commit = MIN2(commit, uncommitted);
  assert(commit <= uncommitted, "sanity");

  if (!_vs->expand_by(commit, false)) {
    vm_exit_during_initialization(err_msg("Failed to expand shared space to " SIZE_FORMAT " bytes",
                                          need_committed_size));
  }

  if (DebugDynamicCDS) {
    dynamic_cds_log->print_cr("Expanding shared spaces by " SIZE_FORMAT_W(7) " bytes [total " SIZE_FORMAT_W(9)  " bytes ending at %p]",
                   commit, _vs->actual_committed_size(), _vs->high());
  }
}

char* DumpRegion::allocate(size_t num_bytes) {
  char* p = (char*)align_up(_top, (size_t)KlassAlignmentInBytes);
  char* newtop = p + align_up(num_bytes, (size_t)KlassAlignmentInBytes);
  expand_top_to(newtop);
  memset(p, 0, newtop - p);
  return p;
}

void DumpRegion::append_intptr_t(intptr_t n, bool need_to_mark) {
  assert(is_aligned(_top, sizeof(intptr_t)), "bad alignment");
  intptr_t *p = (intptr_t*)_top;
  char* newtop = _top + sizeof(intptr_t);
  expand_top_to(newtop);
  *p = n;
  if (need_to_mark) {
    ArchivePtrMarker::mark_pointer(p);
  }
}

void DumpRegion::init(ReservedSpace* rs, VirtualSpace* vs) {
  _rs = rs;
  _vs = vs;
  // Start with 0 committed bytes. The memory will be committed as needed.
  if (!_vs->initialize(*_rs, 0)) {
    fatal("Unable to allocate memory for shared space");
  }
  _base = _top = _rs->base();
  _end = _rs->base() + _rs->size();
}

void DumpRegion::pack(DumpRegion* next) {
  assert(!is_packed(), "sanity");
  _end = (char*)align_up(_top, (size_t)os::vm_allocation_granularity());
  _is_packed = true;
  if (next != NULL) {
    next->_rs = _rs;
    next->_vs = _vs;
    next->_base = next->_top = this->_end;
    next->_end = _rs->base() + _rs->size();
  }
}

void DynamicWriteClosure::do_region(u_char* start, size_t size) {
  assert((intptr_t)start % sizeof(intptr_t) == 0, "bad alignment");
  assert(size % sizeof(intptr_t) == 0, "bad size");
  do_tag((int)size);
  while (size > 0) {
    _dump_region->append_intptr_t(*(intptr_t*)start, true);
    start += sizeof(intptr_t);
    size -= sizeof(intptr_t);
  }
}
