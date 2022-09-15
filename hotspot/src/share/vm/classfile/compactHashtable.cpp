/*
 * Copyright (c) 1997, 2021, Oracle and/or its affiliates. All rights reserved.
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
#include "jvm.h"
#include "cds/archiveBuilder.hpp"
#include "classfile/compactHashtable.hpp"
#include "classfile/javaClasses.hpp"
#include "memory/metadataFactory.hpp"
#include "runtime/arguments.hpp"
#include "runtime/globals.hpp"
#include "runtime/vmThread.hpp"
#include "utilities/align.hpp"
#include "utilities/numberSeq.hpp"

/////////////////////////////////////////////////////
//
// The compact hash table writer implementations
//
CompactHashtableWriter::CompactHashtableWriter(int num_entries,
                                               CompactHashtableStats* stats) {
  Arguments::assert_is_dumping_archive();
  assert(num_entries >= 0, "sanity");
  _num_buckets = calculate_num_buckets(num_entries);
  assert(_num_buckets > 0, "no buckets");

  _num_entries_written = 0;
  _buckets = NEW_C_HEAP_ARRAY(GrowableArray<Entry>*, _num_buckets, mtSymbol);
  for (int i = 0; i < _num_buckets; i++) {
    _buckets[i] = new (ResourceObj::C_HEAP, mtSymbol) GrowableArray<Entry>(0, true, mtSymbol);
  }

  _stats = stats;
  _compact_buckets = NULL;
  _compact_entries = NULL;
  _num_empty_buckets = 0;
  _num_value_only_buckets = 0;
  _num_other_buckets = 0;
}

CompactHashtableWriter::~CompactHashtableWriter() {
  for (int index = 0; index < _num_buckets; index++) {
    GrowableArray<Entry>* bucket = _buckets[index];
    delete bucket;
  }

  FREE_C_HEAP_ARRAY(GrowableArray<Entry>*, _buckets, mtSymbol);
}

size_t CompactHashtableWriter::estimate_size(int num_entries) {
  int num_buckets = calculate_num_buckets(num_entries);
  size_t bucket_bytes = ArchiveBuilder::ro_array_bytesize<u4>(num_buckets + 1);

  // In worst case, we have no VALUE_ONLY_BUCKET_TYPE, so each entry takes 2 slots
  int entries_space = 2 * num_entries;
  size_t entry_bytes = ArchiveBuilder::ro_array_bytesize<u4>(entries_space);

  return bucket_bytes
       + entry_bytes
       + SimpleCompactHashtable::calculate_header_size();
}

// Add a symbol entry to the temporary hash table
void CompactHashtableWriter::add(unsigned int hash, u4 value) {
  int index = hash % _num_buckets;
  _buckets[index]->append_if_missing(Entry(hash, value));
  _num_entries_written++;
}

void CompactHashtableWriter::allocate_table() {
  int entries_space = 0;
  for (int index = 0; index < _num_buckets; index++) {
    GrowableArray<Entry>* bucket = _buckets[index];
    int bucket_size = bucket->length();
    if (bucket_size == 1) {
      entries_space++;
    } else if (bucket_size > 1) {
      entries_space += 2 * bucket_size;
    }
  }

  if (entries_space & ~BUCKET_OFFSET_MASK) {
    vm_exit_during_initialization("CompactHashtableWriter::allocate_table: Overflow! "
                                  "Too many entries.");
  }

  _compact_buckets = ArchiveBuilder::new_ro_array<u4>(_num_buckets + 1);
  _compact_entries = ArchiveBuilder::new_ro_array<u4>(entries_space);

  _stats->bucket_count    = _num_buckets;
  _stats->bucket_bytes    = align_up(_compact_buckets->size() * BytesPerWord,
                                     KlassAlignmentInBytes);
  _stats->hashentry_count = _num_entries_written;
  _stats->hashentry_bytes = align_up(_compact_entries->size() * BytesPerWord,
                                     KlassAlignmentInBytes);
}

// Write the compact table's buckets
void CompactHashtableWriter::dump_table(NumberSeq* summary) {
  u4 offset = 0;
  for (int index = 0; index < _num_buckets; index++) {
    GrowableArray<Entry>* bucket = _buckets[index];
    int bucket_size = bucket->length();
    if (bucket_size == 1) {
      // bucket with one entry is compacted and only has the symbol offset
      _compact_buckets->at_put(index, BUCKET_INFO(offset, VALUE_ONLY_BUCKET_TYPE));

      Entry ent = bucket->at(0);
      _compact_entries->at_put(offset++, ent.value());
      _num_value_only_buckets++;
    } else {
      // regular bucket, each entry is a symbol (hash, offset) pair
      _compact_buckets->at_put(index, BUCKET_INFO(offset, REGULAR_BUCKET_TYPE));

      for (int i=0; i<bucket_size; i++) {
        Entry ent = bucket->at(i);
        _compact_entries->at_put(offset++, u4(ent.hash())); // write entry hash
        _compact_entries->at_put(offset++, ent.value());
      }
      if (bucket_size == 0) {
        _num_empty_buckets++;
      } else {
        _num_other_buckets++;
      }
    }
    summary->add(bucket_size);
  }

  // Mark the end of the buckets
  _compact_buckets->at_put(_num_buckets, BUCKET_INFO(offset, TABLEEND_BUCKET_TYPE));
  assert(offset == (u4)_compact_entries->length(), "sanity");
}

// Write the compact table
void CompactHashtableWriter::dump(SimpleCompactHashtable *cht, const char* table_name) {
  NumberSeq summary;
  allocate_table();
  dump_table(&summary);

  int table_bytes = _stats->bucket_bytes + _stats->hashentry_bytes;
  address base_address = address(SharedBaseAddress);
  cht->init(base_address,  _num_entries_written, _num_buckets,
            _compact_buckets->data(), _compact_entries->data());

  if (InfoDynamicCDS) {
    double avg_cost = 0.0;
    if (_num_entries_written > 0) {
      avg_cost = double(table_bytes)/double(_num_entries_written);
    }
    dynamic_cds_log->print_cr("Shared %s table stats -------- base: " PTR_FORMAT,
                         table_name, (intptr_t)base_address);
    dynamic_cds_log->print_cr("Number of entries       : %9d", _num_entries_written);
    dynamic_cds_log->print_cr("Total bytes used        : %9d", table_bytes);
    dynamic_cds_log->print_cr("Average bytes per entry : %9.3f", avg_cost);
    dynamic_cds_log->print_cr("Average bucket size     : %9.3f", summary.avg());
    dynamic_cds_log->print_cr("Variance of bucket size : %9.3f", summary.variance());
    dynamic_cds_log->print_cr("Std. dev. of bucket size: %9.3f", summary.sd());
    dynamic_cds_log->print_cr("Maximum bucket size     : %9d", (int)summary.maximum());
    dynamic_cds_log->print_cr("Empty buckets           : %9d", _num_empty_buckets);
    dynamic_cds_log->print_cr("Value_Only buckets      : %9d", _num_value_only_buckets);
    dynamic_cds_log->print_cr("Other buckets           : %9d", _num_other_buckets);
  }
}

/////////////////////////////////////////////////////////////
//
// The CompactHashtable implementation
//

void SimpleCompactHashtable::init(address base_address, u4 entry_count, u4 bucket_count, u4* buckets, u4* entries) {
  _bucket_count = bucket_count;
  _entry_count = entry_count;
  _base_address = base_address;
  _buckets = buckets;
  _entries = entries;
}

size_t SimpleCompactHashtable::calculate_header_size() {
  // We have 5 fields. Each takes up sizeof(intptr_t). See WriteClosure::do_u4
  size_t bytes = sizeof(intptr_t) * 5;
  return bytes;
}

void SimpleCompactHashtable::serialize_header(SerializeClosure* soc) {
  // NOTE: if you change this function, you MUST change the number 5 in
  // calculate_header_size() accordingly.
  soc->do_u4(&_entry_count);
  soc->do_u4(&_bucket_count);
  soc->do_ptr((void**)&_buckets);
  soc->do_ptr((void**)&_entries);
  if (soc->reading()) {
    _base_address = (address)SharedBaseAddress;
  }
}
