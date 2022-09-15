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

#ifndef SHARE_VM_CDS_ARCHIVEUTILS_HPP
#define SHARE_VM_CDS_ARCHIVEUTILS_HPP

#include "memory/iterator.hpp"
#include "runtime/virtualspace.hpp"
#include "utilities/bitMap.hpp"

class ArchivePtrMarker : AllStatic {
  static BitMap*  _ptrmap;
  static VirtualSpace* _vs;

  // Once _ptrmap is compacted, we don't allow bit marking anymore. This is to
  // avoid unintentional copy operations after the bitmap has been finalized and written.
  static bool         _compacted;

  static address* ptr_base() { return (address*)_vs->low();  } // committed lower bound (inclusive)
  static address* ptr_end()  { return (address*)_vs->high(); } // committed upper bound (exclusive)

public:
  static void initialize(BitMap* ptrmap, VirtualSpace* vs);
  static void mark_pointer(address* ptr_loc);
  static void clear_pointer(address* ptr_loc);
  static void compact(address relocatable_base, address relocatable_end);
  static void compact(size_t max_non_null_offset);

  template <typename T>
  static void mark_pointer(T* ptr_loc) {
    mark_pointer((address*)ptr_loc);
  }

  template <typename T>
  static void set_and_mark_pointer(T* ptr_loc, T ptr_value) {
    *ptr_loc = ptr_value;
    mark_pointer(ptr_loc);
  }

  static BitMap* ptrmap() {
    return _ptrmap;
  }
};

class DumpRegion {
private:
  const char* _name;
  char* _base;
  char* _top;
  char* _end;
  uintx _max_delta;
  bool _is_packed;
  ReservedSpace* _rs;
  VirtualSpace* _vs;

  void commit_to(char* newtop);

public:
  DumpRegion(const char* name, uintx max_delta = 0)
    : _name(name), _base(NULL), _top(NULL), _end(NULL),
      _max_delta(max_delta), _is_packed(false) {}

  char* expand_top_to(char* newtop);
  char* allocate(size_t num_bytes);

  void append_intptr_t(intptr_t n, bool need_to_mark = false);

  char* base()      const { return _base;        }
  char* top()       const { return _top;         }
  char* end()       const { return _end;         }
  size_t reserved() const { return _end - _base; }
  size_t used()     const { return _top - _base; }
  bool is_packed()  const { return _is_packed;   }
  bool is_allocatable() const {
    return !is_packed() && _base != NULL;
  }

  void print(size_t total_bytes) const;
  void print_out_of_space_msg(const char* failing_region, size_t needed_bytes);

  void init(ReservedSpace* rs, VirtualSpace* vs);

  void pack(DumpRegion* next = NULL);

  bool contains(char* p) const {
    return base() <= p && p < top();
  }
};

// Closure for serializing initialization data out to a data area to be
// written to the shared file.

class DynamicWriteClosure : public SerializeClosure {
private:
  DumpRegion* _dump_region;

public:
  DynamicWriteClosure(DumpRegion* r) {
    _dump_region = r;
  }

  void do_ptr(void** p) {
    _dump_region->append_intptr_t((intptr_t)*p, true);
  }

  void do_u4(u4* p) {
    _dump_region->append_intptr_t((intptr_t)(*p));
  }

  void do_tag(int tag) {
    _dump_region->append_intptr_t((intptr_t)tag);
  }

  //void do_oop(oop* o);
  void do_region(u_char* start, size_t size);
  bool reading() const { return false; }
};

#endif // SHARE_VM_CDS_ARCHIVEUTILS_HPP
