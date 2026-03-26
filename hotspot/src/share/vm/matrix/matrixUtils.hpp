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

#include "runtime/atomic.hpp"
#include "utilities/ostream.hpp"

#define MATRIX_TABLE_SIZE 1024

template <typename V, MEMFLAGS f = mtInternal>
class SimpleList : public CHeapObj<f> {
 private:
  class SimpleListNode : public CHeapObj<f> {
   public:
    SimpleListNode(V v, SimpleList::SimpleListNode *n) : value(v), next(n) {}

    V value;
    SimpleListNode *next;
  };

 public:
  explicit SimpleList(V empty_val) {
    _head = new SimpleListNode(empty_val, NULL);
    _tail = _head;
    _empty_val = empty_val;
  }

  void insert(V value) {
    SimpleListNode *node = _head->next;
    _head->next = new SimpleListNode(value, node);
    if (node == NULL) _tail = _head->next;
  }

  int size() {
    SimpleListNode *tmp = _head->next;
    int length = 0;
    while (tmp != NULL) {
      length += 1;
      tmp = tmp->next;
    }
    return length;
  }

  // thread safe
  void append(V value) {
    SimpleListNode *new_val = new SimpleListNode(value, NULL);

    SimpleListNode *orig_tail = _tail;
    while (Atomic::cmpxchg_ptr(new_val, &_tail, orig_tail) != orig_tail) {
      orig_tail = _tail;
    }
    orig_tail->next = new_val;
  }

  void begin_iteration() { _iter = _head; }

  bool contains(V v) {
    SimpleListNode *cur = _head->next;
    while (cur != NULL) {
      if (cur->value == v) return true;
      cur = cur->next;
    }
    return false;
  }

  V next() {
    _iter = _iter->next;
    if (_iter == NULL) return _empty_val;
    return _iter->value;
  }

  V *cur_value() { return &(_iter->value); }

  bool update(V old_val, V new_val) {
    SimpleListNode *cur = _head->next;
    while (cur != NULL) {
      if (cur->value == old_val) {
        cur->value = new_val;
        return true;
      }
      cur = cur->next;
    }
    return false;
  }

  void clear(void (*cleanup)(V)) {
    SimpleListNode *current = _head->next;
    SimpleListNode *next;
    while (current != NULL) {
      next = current->next;
      cleanup(current->value);
      free(current);
      current = next;
    }
    _head->next = NULL;
    _tail = _head;
  }

 private:
  SimpleListNode *_head;
  SimpleListNode *_tail;
  SimpleListNode *_iter;
  V _empty_val;
};

template <typename V, MEMFLAGS f = mtInternal>
class PtrList : public SimpleList<V, f> {
 public:
  PtrList() : SimpleList<V, f>(NULL) {}
};

template <typename K, typename V, size_t bucket_cnt, MEMFLAGS f = mtInternal>
class SimpleHashTable : public CHeapObj<f> {
 public:
  explicit SimpleHashTable(V empty_value) {
    memset(_buckets, 0, sizeof(_buckets));
    _iter = _buckets[0];
    _bucket_iter = 0;
    _cur_iter = NULL;
    _empty_val = empty_value;
  }

  ~SimpleHashTable() {
    for (size_t i = 0; i < bucket_cnt; ++i) {
      SimpleHashEntry *entry = _buckets[i];
      while (entry != NULL) {
        SimpleHashEntry *next = entry->next;
        delete entry;
        entry = next;
      }
      _buckets[i] = NULL;
    }
    _iter = NULL;
    _cur_iter = NULL;
    _bucket_iter = 0;
  }

  // should be called before every full iteration
  void begin_iteration() {
    _bucket_iter = 0;
    _iter = NULL;
    _cur_iter = NULL;
    while (_bucket_iter < bucket_cnt) {
      if (_buckets[_bucket_iter] != NULL) {
        _iter = _buckets[_bucket_iter];
        _bucket_iter += 1;
        break;
      }
      _bucket_iter += 1;
    }
  }

  V next() {
    _cur_iter = _iter;
    if (_cur_iter == NULL) return _empty_val;
    _iter = _iter->next;
    if (_iter == NULL) {
      while (_buckets[_bucket_iter] == NULL && _bucket_iter < bucket_cnt)
        _bucket_iter += 1;
      if (_bucket_iter == bucket_cnt)
        _iter = NULL;
      else {
        _iter = _buckets[_bucket_iter];
        _bucket_iter += 1;
      }
    }
    return _cur_iter->value;
  }

  K get_cur_iter_key() {
    if (_cur_iter == NULL) return NULL;
    return _cur_iter->key;
  }

  void update_iter_value(V v) {
    guarantee(_cur_iter != NULL, "must be");
    _cur_iter->value = v;
  }

  void dump() {
    for (int i = 0; i < bucket_cnt; i++) {
      tty->print("bucket %d: ", i);
      SimpleHashEntry *entry = _buckets[i];
      while (entry != NULL) {
        tty->print("%p -> ", entry);
        entry = entry->next;
      }
      tty->print_cr("NULL");
    }
  }

  void add(K key, V v) { internal_put(hash(key), key, v); }

  V get(K key) { return internal_get(hash(key), key); }

  void remove(K key) { internal_remove(hash(key), key); }

  V *lookup(K key) {
    size_t hash_val = hash(key);
    size_t bucket = hash_val % bucket_cnt;
    SimpleHashEntry *e = _buckets[bucket];
    while (e != NULL && !equals(e->key, key)) {
      e = e->next;
    }
    return e == NULL ? NULL : &e->value;
  }

  void merge_with(SimpleHashTable<K, V, bucket_cnt> *other) {
    for (int i = 0; i < bucket_cnt; i++) {
      if (other->_buckets[i] == NULL) continue;
      SimpleHashEntry *e = _buckets[i];
      _buckets[i] = other->_buckets[i];

      SimpleHashEntry *cur = _buckets[i];
      while (cur->next != NULL) {
        cur = cur->next;
      }
      cur->next = e;
      // in case double free problem
      other->_buckets[i] = NULL;
    }
  }

 protected:
  virtual size_t hash(K k) = 0;
  virtual bool equals(K k1, K k2) { return k1 == k2; }

  class SimpleHashEntry : public CHeapObj<f> {
   public:
    SimpleHashEntry *next;
    K key;
    V value;

    SimpleHashEntry(SimpleHashEntry *n, K k, V v) : next(n), key(k), value(v) {}
  };

  void internal_put(size_t hash, K key, V value) {
    size_t bucket = hash % bucket_cnt;
    SimpleHashEntry *e = _buckets[bucket];
    if (e == NULL) {
      _buckets[bucket] = new SimpleHashEntry(NULL, key, value);
      return;
    }
    if (equals(e->key, key)) {
      e->value = value;
      return;
    }
    SimpleHashEntry *pre = e, *cur = e->next;
    while (cur != NULL && !equals(cur->key, key)) {
      pre = cur;
      cur = cur->next;
    }
    if (cur != NULL) {
      cur->value = value;
    } else {
      pre->next = new SimpleHashEntry(NULL, key, value);
    }
  }

  void internal_remove(size_t hash, K key) {
    size_t bucket = hash % bucket_cnt;
    SimpleHashEntry *e = _buckets[bucket];
    SimpleHashEntry *cur = e;
    SimpleHashEntry *prev = NULL;
    while (cur != NULL && !equals(cur->key, key)) {
      prev = cur;
      cur = cur->next;
    }
    if (cur == NULL) {
      guarantee(false, "deleting unexisted key");
    }
    if (prev != NULL)
      prev->next = cur->next;
    else
      _buckets[bucket] = cur->next;

    delete cur;
  }

  V internal_get(size_t hash, K key) {
    size_t bucket = hash % bucket_cnt;
    SimpleHashEntry *e = _buckets[bucket];
    while (e != NULL && !equals(e->key, key)) {
      e = e->next;
    }
    return e == NULL ? _empty_val : e->value;
  }

 protected:
  SimpleHashEntry *_buckets[bucket_cnt];

  SimpleHashEntry *_iter;
  size_t _bucket_iter;

  SimpleHashEntry *_cur_iter;

  V _empty_val;
};

inline uint64_t integer_hash(uint64_t h) {
  h ^= h >> 33;
  h *= 0xff51afd7ed558ccd;
  h ^= h >> 33;
  h *= 0xc4ceb9fe1a85ec53;
  h ^= h >> 33;
  return h;
}

template <typename K, typename V, MEMFLAGS f = mtInternal>
class PtrTable : public SimpleHashTable<K, V, MATRIX_TABLE_SIZE, f> {
 public:
  explicit PtrTable(V empty_val)
      : SimpleHashTable<K, V, MATRIX_TABLE_SIZE, f>(empty_val) {}

 protected:
  size_t hash(K k) { return integer_hash(uint64_t(k)); }
};

template <typename K, size_t bucket_cnt, MEMFLAGS f = mtInternal>
class SimpleHashSet : public CHeapObj<f> {
 public:
  SimpleHashSet() { memset(_buckets, 0, sizeof(_buckets)); }
  ~SimpleHashSet() {}

  void add(K key) { internal_put(hash(key), key); }

  void remove(K key) { internal_remove(hash(key), key); }

  bool exist(K key) {
    size_t hash_val = hash(key);
    size_t bucket = hash_val % bucket_cnt;
    SimpleHashEntry *e = _buckets[bucket];
    while (e != NULL && !equals(e->key, key)) {
      e = e->next;
    }
    return e == NULL ? false : true;
  }

 protected:
  virtual size_t hash(K k) = 0;
  virtual bool equals(K k1, K k2) { return k1 == k2; }

  class SimpleHashEntry : public CHeapObj<f> {
   public:
    SimpleHashEntry *next;
    K key;

    SimpleHashEntry(SimpleHashEntry *n, K k) : next(n), key(k) {}
  };

  void internal_put(size_t hash, K key) {
    size_t bucket = hash % bucket_cnt;
    SimpleHashEntry *e = _buckets[bucket];
    if (e == NULL) {
      _buckets[bucket] = new SimpleHashEntry(NULL, key);
      return;
    }
    if (equals(e->key, key)) {
      return;
    }
    SimpleHashEntry *pre = e, *cur = e->next;
    while (cur != NULL && !equals(cur->key, key)) {
      pre = cur;
      cur = cur->next;
    }
    if (cur == NULL) {
      pre->next = new SimpleHashEntry(NULL, key);
    }
  }

  void internal_remove(size_t hash, K key) {
    size_t bucket = hash % bucket_cnt;
    SimpleHashEntry *e = _buckets[bucket];
    SimpleHashEntry *cur = e;
    SimpleHashEntry *prev = NULL;
    while (cur != NULL && !equals(cur->key, key)) {
      prev = cur;
      cur = cur->next;
    }
    if (cur == NULL) {
      // just return no warning
      return;
    }
    if (prev != NULL)
      prev->next = cur->next;
    else
      _buckets[bucket] = cur->next;
  }

 protected:
  SimpleHashEntry *_buckets[bucket_cnt];
};

template <typename K, MEMFLAGS f = mtInternal>
class PtrHashSet : public SimpleHashSet<K, MATRIX_TABLE_SIZE, f> {
 public:
  PtrHashSet() : SimpleHashSet<K, MATRIX_TABLE_SIZE, f>() {}

 protected:
  size_t hash(K k) { return integer_hash(uint64_t(k)); }
};

#endif  // SHARE_VM_MATRIX_UTILS_HPP