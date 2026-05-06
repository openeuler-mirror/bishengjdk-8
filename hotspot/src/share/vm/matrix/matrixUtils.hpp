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

#ifndef SHARE_VM_MATRIX_UTILS_HPP
#define SHARE_VM_MATRIX_UTILS_HPP

#include "utilities/growableArray.hpp"
#include "utilities/hashtable.hpp"
#include "utilities/ostream.hpp"

static const size_t MatrixTableSize = 1024;
static const int MatrixTableMaxSize = 65536;
static const int UB_INIT_ARRAY_CAP = 8;

template <typename V, MEMFLAGS F = mtInternal>
class MatrixList : public CHeapObj<F> {
 public:
  // MatrixList is not thread-safe. Callers must serialize concurrent access.
  explicit MatrixList(V empty_val)
      : _elements(new (ResourceObj::C_HEAP, F) GrowableArray<V>(UB_INIT_ARRAY_CAP, true, F)),
        _iteration_index(-1),
        _empty_val(empty_val) {}

  ~MatrixList() {
    delete _elements;
  }

  void insert(V value) {
    _elements->insert_before(0, value);
  }

  int size() const { return _elements->length(); }

  void append(V value) {
    _elements->append(value);
  }

  void begin_iteration() { _iteration_index = -1; }

  bool contains(V value) const { return _elements->contains(value); }

  V next() {
    if (_iteration_index + 1 >= _elements->length()) {
      _iteration_index = _elements->length();
      return _empty_val;
    }
    _iteration_index++;
    return _elements->at(_iteration_index);
  }

  V* cur_value() {
    guarantee(_iteration_index >= 0 && _iteration_index < _elements->length(),
              "iteration not started");
    return _elements->adr_at(_iteration_index);
  }

  bool update(V old_val, V new_val) {
    for (int i = 0; i < _elements->length(); i++) {
      if (_elements->at(i) == old_val) {
        _elements->at_put(i, new_val);
        return true;
      }
    }
    return false;
  }

  void clear(void (*cleanup)(V)) {
    for (int i = 0; i < _elements->length(); i++) {
      cleanup(_elements->at(i));
    }
    _elements->clear();
    _iteration_index = -1;
  }

 private:
  GrowableArray<V>* _elements;
  int _iteration_index;
  V _empty_val;
};

template <typename K, typename V, size_t bucket_cnt, MEMFLAGS F = mtInternal>
class MatrixHashTable : public BasicHashtable<F> {
 public:
  // MatrixHashTable is not thread-safe. Callers must serialize concurrent access.
  explicit MatrixHashTable(V empty_value)
      : BasicHashtable<F>(bucket_cnt, sizeof(MatrixHashEntry)),
        _bucket_iter(0),
        _iter(NULL),
        _cur_iter(NULL),
        _empty_val(empty_value) {}

  ~MatrixHashTable() { this->free_buckets(); }

  // should be called before every full iteration
  void begin_iteration() {
    _bucket_iter = 0;
    _iter = NULL;
    _cur_iter = NULL;
    advance_bucket();
  }

  V next() {
    _cur_iter = _iter;
    if (_cur_iter == NULL) return _empty_val;
    _iter = _iter->next();
    if (_iter == NULL) advance_bucket();
    return _cur_iter->value;
  }

  K get_cur_iter_key() {
    return _cur_iter == NULL ? K() : _cur_iter->key;
  }

  void update_iter_value(V value) {
    guarantee(_cur_iter != NULL, "must be");
    _cur_iter->value = value;
  }

  void dump() {
    for (int i = 0; i < this->table_size(); i++) {
      tty->print("bucket %d: ", (int)i);
      MatrixHashEntry* entry = bucket(i);
      while (entry != NULL) {
        tty->print("%p -> ", entry);
        entry = entry->next();
      }
      tty->print_cr("NULL");
    }
  }

  void add(K key, V value) { internal_put(hash(key), key, value); }

  V get(K key) { return internal_get(hash(key), key); }

  void remove(K key) { internal_remove(hash(key), key); }

  V* lookup(K key) {
    MatrixHashEntry* entry = find_entry(hash(key), key);
    return entry == NULL ? NULL : &entry->value;
  }

  void merge_with(MatrixHashTable<K, V, bucket_cnt, F>* other) {
    for (int i = 0; i < other->table_size(); i++) {
      MatrixHashEntry* entry = other->bucket((int)i);
      if (entry == NULL) continue;
      other->set_entry(i, NULL);
      while (entry != NULL) {
        MatrixHashEntry* next = entry->next();
        other->unlink_entry(entry);
        int new_bucket_index = this->hash_to_index(entry->hash());
        this->add_entry(new_bucket_index, entry);
        entry = next;
      }
    }
  }

 protected:
  virtual size_t hash(K key) = 0;
  virtual bool equals(K key1, K key2) const { return key1 == key2; }

  class MatrixHashEntry : public BasicHashtableEntry<F> {
   public:
    K key;
    V value;

    MatrixHashEntry* next() const {
      return reinterpret_cast<MatrixHashEntry*>(BasicHashtableEntry<F>::next());
    }
  };

  void internal_put(size_t hash_value, K key, V value) {
    maybe_grow();
    int bucket_index = this->hash_to_index((unsigned int)hash_value);
    MatrixHashEntry* entry = find_entry(hash_value, key);
    if (entry != NULL) {
      entry->value = value;
      return;
    }

    MatrixHashEntry* new_entry = allocate_entry((unsigned int)hash_value, key, value);
    this->add_entry(bucket_index, new_entry);
  }

  void internal_remove(size_t hash_value, K key) {
    int bucket_index = this->hash_to_index((unsigned int)hash_value);
    MatrixHashEntry* prev = NULL;
    MatrixHashEntry* entry = bucket(bucket_index);
    while (entry != NULL && !equals(entry->key, key)) {
      prev = entry;
      entry = entry->next();
    }
    if (entry == NULL) {
      guarantee(false, "deleting unexisted key");
    }
    if (prev == NULL) {
      this->set_entry(bucket_index, entry->next());
    } else {
      prev->set_next(entry->next());
    }
    this->free_entry(entry);
  }

  V internal_get(size_t hash_value, K key) {
    MatrixHashEntry* entry = find_entry(hash_value, key);
    return entry == NULL ? _empty_val : entry->value;
  }

 private:
  MatrixHashEntry* bucket(int index) const {
    return reinterpret_cast<MatrixHashEntry*>(BasicHashtable<F>::bucket(index));
  }

  MatrixHashEntry* allocate_entry(unsigned int hash_value, K key, V value) {
    MatrixHashEntry* entry =
        reinterpret_cast<MatrixHashEntry*>(BasicHashtable<F>::new_entry(hash_value));
    entry->key = key;
    entry->value = value;
    return entry;
  }

  MatrixHashEntry* find_entry(size_t hash_value, K key) const {
    int bucket_index = this->hash_to_index((unsigned int)hash_value);
    MatrixHashEntry* entry = bucket(bucket_index);
    while (entry != NULL && !equals(entry->key, key)) {
      entry = entry->next();
    }
    return entry;
  }

  void advance_bucket() {
    while (_bucket_iter < (size_t)this->table_size()) {
      _iter = bucket((int)_bucket_iter++);
      if (_iter != NULL) return;
    }
    _iter = NULL;
  }

  void maybe_grow() {
    if (this->table_size() >= MatrixTableMaxSize) return;
    if (this->number_of_entries() < this->table_size()) return;
    int new_size = this->table_size() * 2;
    if (new_size > MatrixTableMaxSize) {
      new_size = MatrixTableMaxSize;
    }
    this->resize(new_size);
  }

  size_t _bucket_iter;
  MatrixHashEntry* _iter;
  MatrixHashEntry* _cur_iter;
  V _empty_val;
};

inline bool ub_option_blank(const char* value) {
  return value == NULL || value[0] == '\0';
}

// MurmurHash3 64-bit finalizer constants.
inline uint64_t matrix_integer_hash(uint64_t value) {
  value ^= value >> 33;
  value *= 0xff51afd7ed558ccdULL;
  value ^= value >> 33;
  value *= 0xc4ceb9fe1a85ec53ULL;
  value ^= value >> 33;
  return value;
}

template <typename K, typename V, MEMFLAGS F = mtInternal>
class PtrTable : public MatrixHashTable<K, V, MatrixTableSize, F> {
 public:
  explicit PtrTable(V empty_val)
      : MatrixHashTable<K, V, MatrixTableSize, F>(empty_val) {}

 protected:
  size_t hash(K key) { return matrix_integer_hash(uint64_t(key)); }
};

template <typename K, size_t bucket_cnt, MEMFLAGS F = mtInternal>
class MatrixHashSet : public BasicHashtable<F> {
 public:
  // MatrixHashSet is not thread-safe. Callers must serialize concurrent access.
  MatrixHashSet() : BasicHashtable<F>(bucket_cnt, sizeof(MatrixHashEntry)) {}
  ~MatrixHashSet() { this->free_buckets(); }

  void add(K key) { internal_put(hash(key), key); }

  void remove(K key) { internal_remove(hash(key), key); }

  bool exist(K key) {
    return find_entry(hash(key), key) != NULL;
  }

 protected:
  virtual size_t hash(K key) = 0;
  virtual bool equals(K key1, K key2) const { return key1 == key2; }

  class MatrixHashEntry : public BasicHashtableEntry<F> {
   public:
    K key;

    MatrixHashEntry* next() const {
      return reinterpret_cast<MatrixHashEntry*>(BasicHashtableEntry<F>::next());
    }
  };

  void internal_put(size_t hash_value, K key) {
    maybe_grow();
    int bucket_index = this->hash_to_index((unsigned int)hash_value);
    if (find_entry(hash_value, key) != NULL) {
      return;
    }
    MatrixHashEntry* entry = allocate_entry((unsigned int)hash_value, key);
    this->add_entry(bucket_index, entry);
  }

  void internal_remove(size_t hash_value, K key) {
    int bucket_index = this->hash_to_index((unsigned int)hash_value);
    MatrixHashEntry* prev = NULL;
    MatrixHashEntry* entry = bucket(bucket_index);
    while (entry != NULL && !equals(entry->key, key)) {
      prev = entry;
      entry = entry->next();
    }
    if (entry == NULL) {
      return;
    }
    if (prev == NULL) {
      this->set_entry(bucket_index, entry->next());
    } else {
      prev->set_next(entry->next());
    }
    this->free_entry(entry);
  }

 private:
  MatrixHashEntry* bucket(int index) const {
    return reinterpret_cast<MatrixHashEntry*>(BasicHashtable<F>::bucket(index));
  }

  MatrixHashEntry* allocate_entry(unsigned int hash_value, K key) {
    MatrixHashEntry* entry =
        reinterpret_cast<MatrixHashEntry*>(BasicHashtable<F>::new_entry(hash_value));
    entry->key = key;
    return entry;
  }

  MatrixHashEntry* find_entry(size_t hash_value, K key) const {
    int bucket_index = this->hash_to_index((unsigned int)hash_value);
    MatrixHashEntry* entry = bucket(bucket_index);
    while (entry != NULL && !equals(entry->key, key)) {
      entry = entry->next();
    }
    return entry;
  }

  void maybe_grow() {
    if (this->table_size() >= MatrixTableMaxSize) return;
    if (this->number_of_entries() < this->table_size()) return;
    int new_size = this->table_size() * 2;
    if (new_size > MatrixTableMaxSize) {
      new_size = MatrixTableMaxSize;
    }
    this->resize(new_size);
  }
};

template <typename K, MEMFLAGS F = mtInternal>
class PtrHashSet : public MatrixHashSet<K, MatrixTableSize, F> {
 public:
  PtrHashSet() : MatrixHashSet<K, MatrixTableSize, F>() {}

 protected:
  size_t hash(K key) { return matrix_integer_hash(uint64_t(key)); }
};

#endif  // SHARE_VM_MATRIX_UTILS_HPP
