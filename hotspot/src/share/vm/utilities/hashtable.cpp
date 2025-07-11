/*
 * Copyright (c) 2003, 2017, Oracle and/or its affiliates. All rights reserved.
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
#include "classfile/altHashing.hpp"
#include "classfile/javaClasses.hpp"
#include "memory/allocation.inline.hpp"
#include "memory/filemap.hpp"
#include "memory/resourceArea.hpp"
#include "oops/oop.inline.hpp"
#include "runtime/safepoint.hpp"
#include "utilities/dtrace.hpp"
#include "utilities/hashtable.hpp"
#include "utilities/hashtable.inline.hpp"
#include "utilities/numberSeq.hpp"
#include "utilities/align.hpp"

// This hashtable is implemented as an open hash table with a fixed number of buckets.

template <MEMFLAGS F> BasicHashtableEntry<F>* BasicHashtable<F>::new_entry_free_list() {
  BasicHashtableEntry<F>* entry = NULL;
  if (_free_list != NULL) {
    entry = _free_list;
    _free_list = _free_list->next();
  }
  return entry;
}

// HashtableEntrys are allocated in blocks to reduce the space overhead.
template <MEMFLAGS F> BasicHashtableEntry<F>* BasicHashtable<F>::new_entry(unsigned int hashValue) {
  BasicHashtableEntry<F>* entry = new_entry_free_list();

  if (entry == NULL) {
    if (_first_free_entry + _entry_size >= _end_block) {
      int block_size = MIN2(512, MAX2((int)_table_size / 2, (int)_number_of_entries));
      int len = _entry_size * block_size;
      len = 1 << log2_int(len); // round down to power of 2
      assert(len >= _entry_size, "");
      _first_free_entry = NEW_C_HEAP_ARRAY2(char, len, F, CURRENT_PC);
      memset(_first_free_entry, 0, len);
      _end_block = _first_free_entry + len;
    }
    entry = (BasicHashtableEntry<F>*)_first_free_entry;
    _first_free_entry += _entry_size;
  }

  assert(_entry_size % HeapWordSize == 0, "");
  entry->set_hash(hashValue);
  return entry;
}


template <class T, MEMFLAGS F> HashtableEntry<T, F>* Hashtable<T, F>::new_entry(unsigned int hashValue, T obj) {
  HashtableEntry<T, F>* entry;

  entry = (HashtableEntry<T, F>*)BasicHashtable<F>::new_entry(hashValue);
  entry->set_literal(obj);
  return entry;
}

// Check to see if the hashtable is unbalanced.  The caller set a flag to
// rehash at the next safepoint.  If this bucket is 60 times greater than the
// expected average bucket length, it's an unbalanced hashtable.
// This is somewhat an arbitrary heuristic but if one bucket gets to
// rehash_count which is currently 100, there's probably something wrong.

template <class T, MEMFLAGS F> bool RehashableHashtable<T, F>::check_rehash_table(int count) {
  assert(this->table_size() != 0, "underflow");
  if (count > (((double)this->number_of_entries()/(double)this->table_size())*rehash_multiple)) {
    // Set a flag for the next safepoint, which should be at some guaranteed
    // safepoint interval.
    return true;
  }
  return false;
}

// Create a new table and using alternate hash code, populate the new table
// with the existing elements.   This can be used to change the hash code
// and could in the future change the size of the table.

template <class T, MEMFLAGS F> void RehashableHashtable<T, F>::move_to(RehashableHashtable<T, F>* new_table) {

  // Initialize the global seed for hashing.
  _seed = AltHashing::compute_seed();
  assert(seed() != 0, "shouldn't be zero");

  int saved_entry_count = this->number_of_entries();

  // Iterate through the table and create a new entry for the new table
  for (int i = 0; i < new_table->table_size(); ++i) {
    for (HashtableEntry<T, F>* p = this->bucket(i); p != NULL; ) {
      HashtableEntry<T, F>* next = p->next();
      T string = p->literal();
      // Use alternate hashing algorithm on the symbol in the first table
      unsigned int hashValue = string->new_hash(seed());
      // Get a new index relative to the new table (can also change size)
      int index = new_table->hash_to_index(hashValue);
      p->set_hash(hashValue);
      // Keep the shared bit in the Hashtable entry to indicate that this entry
      // can't be deleted.   The shared bit is the LSB in the _next field so
      // walking the hashtable past these entries requires
      // BasicHashtableEntry::make_ptr() call.
      bool keep_shared = p->is_shared();
      this->unlink_entry(p);
      new_table->add_entry(index, p);
      if (keep_shared) {
        p->set_shared();
      }
      p = next;
    }
  }
  // give the new table the free list as well
  new_table->copy_freelist(this);
  assert(new_table->number_of_entries() == saved_entry_count, "lost entry on dictionary copy?");

  // Destroy memory used by the buckets in the hashtable.  The memory
  // for the elements has been used in a new table and is not
  // destroyed.  The memory reuse will benefit resizing the SystemDictionary
  // to avoid a memory allocation spike at safepoint.
  BasicHashtable<F>::free_buckets();
}

template <MEMFLAGS F> void BasicHashtable<F>::free_buckets() {
  if (NULL != _buckets) {
    // Don't delete the buckets in the shared space.  They aren't
    // allocated by os::malloc
    if (!UseSharedSpaces ||
        !MetaspaceShared::is_in_shared_space(_buckets)) {
       FREE_C_HEAP_ARRAY(HashtableBucket, _buckets, F);
    }
    _buckets = NULL;
  }
}


// Reverse the order of elements in the hash buckets.

template <MEMFLAGS F> void BasicHashtable<F>::reverse() {

  for (int i = 0; i < _table_size; ++i) {
    BasicHashtableEntry<F>* new_list = NULL;
    BasicHashtableEntry<F>* p = bucket(i);
    while (p != NULL) {
      BasicHashtableEntry<F>* next = p->next();
      p->set_next(new_list);
      new_list = p;
      p = next;
    }
    *bucket_addr(i) = new_list;
  }
}

template <MEMFLAGS F> void BasicHashtable<F>::BucketUnlinkContext::free_entry(BasicHashtableEntry<F>* entry) {
  entry->set_next(_removed_head);
  _removed_head = entry;
  if (_removed_tail == NULL) {
    _removed_tail = entry;
  }
  _num_removed++;
}

template <MEMFLAGS F> void BasicHashtable<F>::bulk_free_entries(BucketUnlinkContext* context) {
  if (context->_num_removed == 0) {
    assert(context->_removed_head == NULL && context->_removed_tail == NULL,
           err_msg("Zero entries in the unlink context, but elements linked from " PTR_FORMAT " to " PTR_FORMAT,
                   p2i(context->_removed_head), p2i(context->_removed_tail)));
    return;
  }

  // MT-safe add of the list of BasicHashTableEntrys from the context to the free list.
  BasicHashtableEntry<F>* current = _free_list;
  while (true) {
    context->_removed_tail->set_next(current);
    BasicHashtableEntry<F>* old = (BasicHashtableEntry<F>*)Atomic::cmpxchg_ptr(context->_removed_head, &_free_list, current);
    if (old == current) {
      break;
    }
    current = old;
  }
  Atomic::add(-context->_num_removed, &_number_of_entries);
}

// Copy the table to the shared space.

template <MEMFLAGS F> void BasicHashtable<F>::copy_table(char** top, char* end) {

  // Dump the hash table entries.

  intptr_t *plen = (intptr_t*)(*top);
  *top += sizeof(*plen);

  int i;
  for (i = 0; i < _table_size; ++i) {
    for (BasicHashtableEntry<F>** p = _buckets[i].entry_addr();
                              *p != NULL;
                               p = (*p)->next_addr()) {
      if (*top + entry_size() > end) {
        report_out_of_shared_space(SharedMiscData);
      }
      *p = (BasicHashtableEntry<F>*)memcpy(*top, (void*)*p, entry_size());
      *top += entry_size();
    }
  }
  *plen = ((char*)(*top) - (char*)plen) - sizeof(*plen);

  // Set the shared bit.

  for (i = 0; i < _table_size; ++i) {
    for (BasicHashtableEntry<F>* p = bucket(i); p != NULL; p = p->next()) {
      p->set_shared();
    }
  }
}



// Reverse the order of elements in the hash buckets.

template <class T, MEMFLAGS F> void Hashtable<T, F>::reverse(void* boundary) {

  for (int i = 0; i < this->table_size(); ++i) {
    HashtableEntry<T, F>* high_list = NULL;
    HashtableEntry<T, F>* low_list = NULL;
    HashtableEntry<T, F>* last_low_entry = NULL;
    HashtableEntry<T, F>* p = bucket(i);
    while (p != NULL) {
      HashtableEntry<T, F>* next = p->next();
      if ((void*)p->literal() >= boundary) {
        p->set_next(high_list);
        high_list = p;
      } else {
        p->set_next(low_list);
        low_list = p;
        if (last_low_entry == NULL) {
          last_low_entry = p;
        }
      }
      p = next;
    }
    if (low_list != NULL) {
      *bucket_addr(i) = low_list;
      last_low_entry->set_next(high_list);
    } else {
      *bucket_addr(i) = high_list;
    }
  }
}

template <class T, MEMFLAGS F> int RehashableHashtable<T, F>::literal_size(Symbol *symbol) {
  return symbol->size() * HeapWordSize;
}

template <class T, MEMFLAGS F> int RehashableHashtable<T, F>::literal_size(oop oop) {
  // NOTE: this would over-count if (pre-JDK8) java_lang_Class::has_offset_field() is true,
  // and the String.value array is shared by several Strings. However, starting from JDK8,
  // the String.value array is not shared anymore.
  assert(oop != NULL && oop->klass() == SystemDictionary::String_klass(), "only strings are supported");
  return (oop->size() + java_lang_String::value(oop)->size()) * HeapWordSize;
}

// Dump footprint and bucket length statistics
//
// Note: if you create a new subclass of Hashtable<MyNewType, F>, you will need to
// add a new function Hashtable<T, F>::literal_size(MyNewType lit)

template <class T, MEMFLAGS F> void RehashableHashtable<T, F>::dump_table(outputStream* st, const char *table_name) {
  NumberSeq summary;
  int literal_bytes = 0;
  for (int i = 0; i < this->table_size(); ++i) {
    int count = 0;
    for (HashtableEntry<T, F>* e = this->bucket(i);
       e != NULL; e = e->next()) {
      count++;
      literal_bytes += literal_size(e->literal());
    }
    summary.add((double)count);
  }
  double num_buckets = summary.num();
  double num_entries = summary.sum();

  int bucket_bytes = (int)num_buckets * sizeof(HashtableBucket<F>);
  int entry_bytes  = (int)num_entries * sizeof(HashtableEntry<T, F>);
  int total_bytes = literal_bytes +  bucket_bytes + entry_bytes;

  double bucket_avg  = (num_buckets <= 0) ? 0 : (bucket_bytes  / num_buckets);
  double entry_avg   = (num_entries <= 0) ? 0 : (entry_bytes   / num_entries);
  double literal_avg = (num_entries <= 0) ? 0 : (literal_bytes / num_entries);

  st->print_cr("%s statistics:", table_name);
  st->print_cr("Number of buckets       : %9d = %9d bytes, avg %7.3f", (int)num_buckets, bucket_bytes,  bucket_avg);
  st->print_cr("Number of entries       : %9d = %9d bytes, avg %7.3f", (int)num_entries, entry_bytes,   entry_avg);
  st->print_cr("Number of literals      : %9d = %9d bytes, avg %7.3f", (int)num_entries, literal_bytes, literal_avg);
  st->print_cr("Total footprint         : %9s = %9d bytes", "", total_bytes);
  st->print_cr("Average bucket size     : %9.3f", summary.avg());
  st->print_cr("Variance of bucket size : %9.3f", summary.variance());
  st->print_cr("Std. dev. of bucket size: %9.3f", summary.sd());
  st->print_cr("Maximum bucket size     : %9d", (int)summary.maximum());
}

// Dump the hash table buckets.

template <MEMFLAGS F> void BasicHashtable<F>::copy_buckets(char** top, char* end) {
  intptr_t len = _table_size * sizeof(HashtableBucket<F>);
  *(intptr_t*)(*top) = len;
  *top += sizeof(intptr_t);

  *(intptr_t*)(*top) = _number_of_entries;
  *top += sizeof(intptr_t);

  if (*top + len > end) {
    report_out_of_shared_space(SharedMiscData);
  }
  _buckets = (HashtableBucket<F>*)memcpy(*top, (void*)_buckets, len);
  *top += len;
}

template <MEMFLAGS F> bool BasicHashtable<F>::resize(int new_size) {

  // Allocate new buckets
  HashtableBucket<F>* buckets_new = NEW_C_HEAP_ARRAY2_RETURN_NULL(HashtableBucket<F>, new_size, F, CURRENT_PC);
  if (buckets_new == NULL) {
    return false;
  }

  // Clear the new buckets
  for (int i = 0; i < new_size; i++) {
    buckets_new[i].clear();
  }

  int table_size_old = _table_size;
  // hash_to_index() uses _table_size, so switch the sizes now
  _table_size = new_size;

  // Move entries from the old table to a new table
  for (int index_old = 0; index_old < table_size_old; index_old++) {
    for (BasicHashtableEntry<F>* p = _buckets[index_old].get_entry(); p != NULL; ) {
      BasicHashtableEntry<F>* next = p->next();
      int index_new = hash_to_index(p->hash());

      p->set_next(buckets_new[index_new].get_entry());
      buckets_new[index_new].set_entry(p);
      p = next;
    }
  }

  // The old backets now can be released
  BasicHashtable<F>::free_buckets();

  // Switch to the new storage
  _buckets = buckets_new;

  return true;
}

template <MEMFLAGS F> bool BasicHashtable<F>::maybe_grow(int max_size, int load_factor) {
  assert(SafepointSynchronize::is_at_safepoint(), "must be at safepoint");

  if (table_size() >= max_size) {
    return false;
  }
  if (number_of_entries() / table_size() > load_factor) {
    resize(MIN2<int>(table_size() * 2, max_size));
    return true;
  } else {
    return false;
  }
}

#ifndef PRODUCT

template <class T, MEMFLAGS F> void Hashtable<T, F>::print() {
  ResourceMark rm;

  for (int i = 0; i < BasicHashtable<F>::table_size(); i++) {
    HashtableEntry<T, F>* entry = bucket(i);
    while(entry != NULL) {
      tty->print("%d : ", i);
      entry->literal()->print();
      tty->cr();
      entry = entry->next();
    }
  }
}

template <MEMFLAGS F> void BasicHashtable<F>::verify() {
  int count = 0;
  for (int i = 0; i < table_size(); i++) {
    for (BasicHashtableEntry<F>* p = bucket(i); p != NULL; p = p->next()) {
      ++count;
    }
  }
  assert(count == number_of_entries(), "number of hashtable entries incorrect");
}


#endif // PRODUCT


#ifdef ASSERT

template <MEMFLAGS F> void BasicHashtable<F>::verify_lookup_length(double load) {
  if ((double)_lookup_length / (double)_lookup_count > load * 2.0) {
    warning("Performance bug: SystemDictionary lookup_count=%d "
            "lookup_length=%d average=%lf load=%f",
            _lookup_count, _lookup_length,
            (double) _lookup_length / _lookup_count, load);
  }
}

#endif
// Explicitly instantiate these types
#if INCLUDE_ALL_GCS
template class Hashtable<nmethod*, mtGC>;
template class HashtableEntry<nmethod*, mtGC>;
template class BasicHashtable<mtGC>;
#endif
template class Hashtable<ConstantPool*, mtClass>;
template class RehashableHashtable<Symbol*, mtSymbol>;
template class RehashableHashtable<oopDesc*, mtSymbol>;
template class Hashtable<Symbol*, mtInternal>;
template class Hashtable<Symbol*, mtSymbol>;
template class Hashtable<Klass*, mtClass>;
template class Hashtable<oop, mtClass>;
#if defined(SOLARIS) || defined(CHECK_UNHANDLED_OOPS)
template class Hashtable<oop, mtSymbol>;
template class RehashableHashtable<oop, mtSymbol>;
#endif // SOLARIS || CHECK_UNHANDLED_OOPS
template class Hashtable<oopDesc*, mtSymbol>;
template class Hashtable<Symbol*, mtClass>;
template class HashtableEntry<Symbol*, mtSymbol>;
template class HashtableEntry<Symbol*, mtClass>;
template class HashtableEntry<oop, mtSymbol>;
template class BasicHashtableEntry<mtSymbol>;
template class BasicHashtableEntry<mtCode>;
template class BasicHashtable<mtClass>;
template class BasicHashtable<mtSymbol>;
template class BasicHashtable<mtCode>;
template class BasicHashtable<mtInternal>;
template class BasicHashtable<mtClassShared>;
template class Hashtable<Method*, mtInternal>;
template class Hashtable<Method*, mtNone>;
template class BasicHashtable<mtNone>;
template class Hashtable<Symbol*, mtNone>;
