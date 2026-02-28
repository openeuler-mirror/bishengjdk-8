/*
 * Copyright (c) 2025, Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2019 Alibaba Group Holding Limited. All rights reserved.
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
 */

#include "precompiled.hpp"
#include "jprofilecache/jitProfileCacheHolders.hpp"
#include "classfile/classLoaderData.hpp"
#include "memory/iterator.hpp"

#define METHOD_LIST_INITIAL_CAPACITY  16

ProfileCacheMethodHold::ProfileCacheMethodHold(Symbol* name, Symbol* signature)
  : _method_name(name),
    _method_signature(signature),
    _method_size(0),
    _method_hash(0),
    _interpreter_invocation_count(0),
    _interpreter_exception_count(0),
    _invocation_count(0),
    _backage_count(0),
    _mounted_offset(-1),
    _owns_profile_list(true),
    _next(NULL),
    _resolved_method(NULL),
    _profile_list(new (ResourceObj::C_HEAP, mtClass)
             GrowableArray<BytecodeProfileRecord*>(METHOD_LIST_INITIAL_CAPACITY, mtClass)) {
}

ProfileCacheMethodHold::ProfileCacheMethodHold(ProfileCacheMethodHold& rhs)
  : _method_name(rhs._method_name),
    _method_signature(rhs._method_signature),
    _method_size(rhs._method_size),
    _method_hash(rhs._method_hash),
    _interpreter_invocation_count(rhs._interpreter_invocation_count),
    _interpreter_exception_count(rhs._interpreter_exception_count),
    _invocation_count(rhs._invocation_count),
    _backage_count(rhs._backage_count),
    _mounted_offset(rhs._mounted_offset),
    _owns_profile_list(false),
    _next(NULL),
    _resolved_method(NULL),
    _profile_list(rhs._profile_list) {
}

ProfileCacheMethodHold::~ProfileCacheMethodHold() {
  if (_owns_profile_list) {
    for (int i = 0; i < _profile_list->length(); i++) {
      delete _profile_list->at(i);
    }
    delete _profile_list;
  }
}

bool ProfileCacheMethodHold::is_method_match(Method* method) {
  if (method_name()->fast_compare(method->name()) == 0
    && method_signature()->fast_compare(method->signature()) == 0) {
    return true;
  } else {
    return false;
  }
}

bool ProfileCacheMethodHold::is_alive() const {
  return is_alive(NULL);
}

bool ProfileCacheMethodHold::is_alive(BoolObjectClosure* is_alive_closure) const {
  if (_resolved_method == NULL || _resolved_method->constMethod() == NULL || _resolved_method->constants() == NULL || _resolved_method->constants()->pool_holder() == NULL) {
    return false;
  }
  ClassLoaderData* data = _resolved_method->method_holder()->class_loader_data();
  AlwaysTrueClosure always_true;
  if (is_alive_closure == NULL) {
    is_alive_closure = &always_true;
  }
  if (data == NULL || !data->is_alive(is_alive_closure)) {
    return false;
  }
  return true;
}

ProfileCacheMethodHold* ProfileCacheMethodHold::clone_and_add() {
  ProfileCacheMethodHold* clone = new ProfileCacheMethodHold(*this);
  clone->set_next(_next);
  _next = clone;
  return clone;
}

#define CLASS_METHOD_LIST_INITIAL_CAPACITY  16

ProfileCacheClassHolder::ProfileCacheClassHolder(Symbol* name, Symbol* loader_name,
                                       Symbol* path, unsigned int size,
                                       unsigned int hash, unsigned int crc32)
  : _class_name(name),
    _class_loader_name(loader_name),
    _class_path(path),
    _class_size(size),
    _class_hash(hash),
    _class_crc32(crc32),
    _class_resolved(false),
    _class_method_list(new (ResourceObj::C_HEAP, mtInternal)
                 GrowableArray<ProfileCacheMethodHold*>(CLASS_METHOD_LIST_INITIAL_CAPACITY, mtClass)),
    _next(NULL) {
}


ProfileCacheClassHolder::~ProfileCacheClassHolder() {
  delete _class_method_list;
}
