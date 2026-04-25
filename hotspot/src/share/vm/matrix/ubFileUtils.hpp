/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
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
 */

#ifndef SHARE_VM_MATRIX_UBFILEUTILS_HPP
#define SHARE_VM_MATRIX_UBFILEUTILS_HPP

#include "matrix/matrixUtils.hpp"
#include "utilities/hashtable.hpp"

template <typename K, typename V, MEMFLAGS F,
          unsigned (*HASH)(K const&) = primitive_hash<K>,
          bool (*EQUALS)(K const&, K const&) = primitive_equals<K> >
class UBFileHashTable : public BasicHashtable<F> {
  class UBFileHashTableEntry : public BasicHashtableEntry<F> {
   public:
    K _key;
    V _value;
    UBFileHashTableEntry* next() {
      return (UBFileHashTableEntry*)BasicHashtableEntry<F>::next();
    }
  };

 public:
  explicit UBFileHashTable(int table_size = MatrixTableSize)
      : BasicHashtable<F>(table_size, sizeof(UBFileHashTableEntry)) {}
  ~UBFileHashTable() { this->free_buckets(); }

  V* add(K key, V value) {
    unsigned int hash = HASH(key);
    UBFileHashTableEntry* entry = new_entry(hash, key, value);
    BasicHashtable<F>::safe_add_entry(BasicHashtable<F>::hash_to_index(hash),
                                      entry);
    return &(entry->_value);
  }

  V* add_if_absent(K key, V value) {
    unsigned int hash = HASH(key);
    int index = BasicHashtable<F>::hash_to_index(hash);
    for (UBFileHashTableEntry* e = bucket(index); e != NULL; e = e->next()) {
      if (e->hash() == hash && EQUALS(e->_key, key)) {
        // not safe
        e->_value = value;
        return &(e->_value);
      }
    }
    UBFileHashTableEntry* entry = new_entry(hash, key, value);
    BasicHashtable<F>::safe_add_entry(BasicHashtable<F>::hash_to_index(hash),
                                      entry);
    return &(entry->_value);
  }

  V* lookup(K key) const {
    unsigned int hash = HASH(key);
    int index = BasicHashtable<F>::hash_to_index(hash);
    for (UBFileHashTableEntry* e = bucket(index); e != NULL; e = e->next()) {
      if (e->hash() == hash && EQUALS(e->_key, key)) {
        return &(e->_value);
      }
    }
    return NULL;
  }

  V* print_key(K key) const {
    unsigned int hash = HASH(key);
    int index = BasicHashtable<F>::hash_to_index(hash);
    tty->print_cr("hash %d index %d bucket %p", hash, index, bucket(index));
    for (UBFileHashTableEntry* e = bucket(index); e != NULL; e = e->next()) {
      tty->print_cr("key %p hash %d ", (void*)key, hash);
    }
    return NULL;
  }

  bool remove(K key) {
    unsigned int hash = HASH(key);
    int index = BasicHashtable<F>::hash_to_index(hash);
    UBFileHashTableEntry* previous = NULL;
    for (UBFileHashTableEntry* e = bucket(index); e != NULL;
         previous = e, e = e->next()) {
      if (e->hash() == hash && EQUALS(e->_key, key)) {
        if (previous == NULL) {
          BasicHashtable<F>::safe_set_entry(index, e->next());
        } else {
          previous->set_next(e->next());
        }
        BasicHashtable<F>::free_entry(e);
        return true;
      }
    }
    return false;
  }

 protected:
  UBFileHashTableEntry* bucket(int i) const {
    return (UBFileHashTableEntry*)BasicHashtable<F>::bucket(i);
  }

  UBFileHashTableEntry* new_entry(unsigned int hashValue, K key, V value) {
    UBFileHashTableEntry* entry =
        (UBFileHashTableEntry*)BasicHashtable<F>::new_entry(hashValue);
    entry->_key = key;
    entry->_value = value;
    return entry;
  }
};

#endif  // SHARE_VM_MATRIX_UBFILEUTILS_HPP
