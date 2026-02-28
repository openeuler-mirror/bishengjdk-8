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
#include "classfile/classLoaderData.hpp"
#include "classfile/symbolTable.hpp"
#include "classfile/systemDictionary.hpp"
#include "compiler/compileBroker.hpp"
#include "jprofilecache/jitProfileCacheClass.hpp"
#include "jprofilecache/jitProfileCacheUtils.hpp"
#include "jprofilecache/jitProfileClassChain.hpp"
#include "jprofilecache/jitProfileCacheLog.hpp"
#include "oops/method.hpp"
#include "oops/typeArrayKlass.hpp"
#include "memory/iterator.hpp"
#include "runtime/arguments.hpp"
#include "runtime/deoptimization.hpp"
#include "runtime/handles.inline.hpp"
#include "runtime/javaCalls.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/os.hpp"
#include "runtime/safepoint.hpp"
#include "runtime/thread.hpp"
#include "runtime/atomic.hpp"

ProfileCacheMethodHold* MethodHolderIterator::next() {
  ProfileCacheMethodHold* next_holder = _current_method_hold->next();
  if (next_holder != NULL) {
    _current_method_hold = next_holder;
    return _current_method_hold;
  }
  while (_holder_index > 0) {
    _holder_index--;
    ProfileCacheClassChain::ProfileCacheClassChainEntry* entry = _profile_cache_class_chain->at(_holder_index);
    if (entry->method_holder() != NULL) {
      _current_method_hold = entry->method_holder();
      return _current_method_hold;
    }
  }
  _current_method_hold = NULL;
  return _current_method_hold;
}

bool ProfileCacheClassChain::ProfileCacheClassChainEntry::is_all_initialized() {
  int len = resolved_klasses()->length();
  // if resolved klass is empty return false
  if (len == 0) {
    return false;
  }
  for (int i = 0; i < len; i++) {
    InstanceKlass* k = resolved_klasses()->at(i);
    if (k != NULL && k->is_not_initialized() && !k->is_in_error_state() ) {
      return false;
    }
  }
  return true;
}

bool ProfileCacheClassChain::ProfileCacheClassChainEntry::is_all_linked() {
  int len = resolved_klasses()->length();
  if (len == 0) {
    return false;
  }
  for (int i = 0; i < len; i++) {
    InstanceKlass* k = resolved_klasses()->at(i);
    if (k != NULL && !k->is_linked() && !k->is_in_error_state()) {
      return false;
    }
  }
  return true;
}

bool ProfileCacheClassChain::ProfileCacheClassChainEntry::contains_redefined_class() {
  int len = resolved_klasses()->length();
  for (int i = 0; i < len; i++) {
    InstanceKlass* k = resolved_klasses()->at(i);
    if (k != NULL && k->has_been_redefined()) {
      ResourceMark rm;
      jprofilecache_log_warning(jprofilecache, "[JitProfileCache] WARNING: ignore redefined class after API"
                          " triggerPrecompilation : %s:%s@%s.", class_name()->as_C_string(),
                          class_loader_name()->as_C_string(), class_path()->as_C_string());
      return true;
    }
  }
  return false;
}

InstanceKlass* ProfileCacheClassChain::ProfileCacheClassChainEntry::get_first_uninitialized_klass() {
  int len = resolved_klasses()->length();
  for (int i = 0; i < len; i++) {
    InstanceKlass* k = resolved_klasses()->at(i);
    if (k != NULL && k->is_not_initialized()) {
      return k;
    }
  }
  return NULL;
}

InstanceKlass* ProfileCacheClassChain::ProfileCacheClassChainEntry::get_first_unlinked_klass() {
  int len = resolved_klasses()->length();
  for (int i = 0; i < len; i++) {
    InstanceKlass* k = resolved_klasses()->at(i);
    if (k != NULL && !k->is_linked() && !k->is_in_error_state()) {
      return k;
    }
  }
  return NULL;
}

ProfileCacheClassChain::ProfileCacheClassChain(unsigned int size)
  : _class_chain_inited_index(-1),
    _loaded_class_index(-1),
    _length(size),
    _state(NOT_INITED),
    _entries(new ProfileCacheClassChainEntry[size]),
    _init_timestamp(),
    _last_timestamp(),
    _deopt_index(-1),
    _deopt_cur_holder(NULL),
    _has_unmarked_compiling_flag(false) {
  _init_timestamp.update();
  _last_timestamp.update();
  try_transition_to_state(INITED);
}

ProfileCacheClassChain::~ProfileCacheClassChain() {
  delete[] _entries;
}

const char* ProfileCacheClassChain::get_state(ClassChainState state) {
  switch (state) {
    case NOT_INITED:
      return "not init";
    case INITED:
      return "inited";
    case PRE_PROFILECACHE:
      return "notify precompile";
    case PROFILECACHE_COMPILING:
      return "precompiling";
    case PROFILECACHE_DONE:
      return "precompile done";
    case PROFILECACHE_ERROR_STATE:
      return "profilecache error state";
  }
  assert(false, "invalid state");
  return NULL;
}

bool ProfileCacheClassChain::try_transition_to_state(ClassChainState new_state) {
  ClassChainState old_state = current_state();
  if (old_state == new_state) {
    jprofilecache_log_warning(jprofilecache, "JProfileCache [WARNING]: profilecache state has already been %s Doesn't need transferred to %s",
                                            get_state(old_state), get_state(new_state));
    return true;
  }
  bool can_transfer = false;
  switch (new_state) {
    case PROFILECACHE_ERROR_STATE:
      can_transfer = true;
      break;
    default:
      if (new_state == old_state + 1) {
        can_transfer = true;
      }
      break;
  }
  if (can_transfer) {
    if (Atomic::cmpxchg((jint)new_state, (volatile jint*)&_state, (jint)old_state) == old_state) {
      return true;
    } else {
      jprofilecache_log_warning(jprofilecache, "JProfileCache [WARNING]: failed to transfer profilecache state from %s to %s, conflict with other operation",
                                              get_state(old_state), get_state(new_state));
      return false;
    }
  } else {
    jprofilecache_log_warning(jprofilecache, "JProfileCache [WARNING]: can not transfer profilecache state from %s to %s",
                                            get_state(old_state), get_state(new_state));
    return false;
  }
}

void ProfileCacheClassChain::mark_loaded_class(InstanceKlass* k, ProfileCacheClassEntry* class_entry) {
  Symbol* class_name = k->name();
  unsigned int crc32 = k->crc32();
  unsigned int size = k->bytes_size();

  if (!can_record_class()) {
    return;
  }

  if (class_entry == NULL) {
    return;
  }
  int chain_index = class_entry->chain_offset();
  ProfileCacheClassHolder* holder = class_entry->find_class_holder(size, crc32);
  if (holder != NULL) {
    if (holder->resolved()) {
      handle_duplicate_class(k, chain_index);
      return;
    } else {
      resolve_class_methods(k, holder, chain_index);
    }
  } else {
    ResourceMark rm;
    jprofilecache_log_debug(jprofilecache, "[JitProfileCache] DEBUG : class %s is not in profile",
                      k->name()->as_C_string());
  }

  update_class_chain(k, chain_index);
}

void ProfileCacheClassChain::handle_duplicate_class(InstanceKlass *k, int chain_index) {
  Thread *const t = Thread::current();
  if (!t->is_super_class_resolution_active()) {
    assert(k->is_not_initialized(), "Invalid klass state");
    assert(t->is_Java_thread(), "Thread type mismatch");
    ResourceMark rm;
    jprofilecache_log_warning(jprofilecache, "[JitProfileCache] WARNING : Duplicate load class %s at index %d",
                        k->name()->as_C_string(), chain_index);
  }
}

void ProfileCacheClassChain::resolve_class_methods(InstanceKlass* k, ProfileCacheClassHolder* holder, int chain_index) {
  MutexLocker mu(ProfileCacheClassChain_lock);
  int methods = k->methods()->length();
  for (int index = 0; index < methods; index++) {
    Method* m = k->methods()->at(index);
    resolve_method_info(m, holder);
  }
  {
    ResourceMark rm;
    jprofilecache_log_debug(jprofilecache, "[JitProfileCache] DEBUG : class %s at index %d method_list has been recorded",
                      k->name()->as_C_string(), chain_index);
  }
  holder->set_resolved();
}

void ProfileCacheClassChain::update_class_chain(InstanceKlass* k, int chain_index) {
  MutexLocker mu(ProfileCacheClassChain_lock);
  assert(chain_index >= 0 && chain_index <= length(), "index out of bound");
  assert(loaded_index() >= class_chain_inited_index(), "loaded index must larger than inited index");
  ProfileCacheClassChainEntry* chain_entry = &_entries[chain_index];

  // check class state is skip or init return
  if (chain_entry->is_skipped()) {
    ResourceMark rm;
    char* class_name = k->name()->as_C_string();
    int index = chain_index;
    return;
  } else if (chain_entry->is_inited()) {
    return;
  }
  // set class reserved
  chain_entry->resolved_klasses()->append(k);
  Thread* thread = Thread::current();
  chain_entry->method_keep_holders()->append(JNIHandles::make_global(Handle(thread, k->klass_holder())));

  chain_entry->set_loaded();

  if (chain_index == loaded_index() + 1) {
    update_loaded_index(chain_index);
  }
}

void ProfileCacheClassChain::add_method_at_index(ProfileCacheMethodHold* mh, int index) {
  assert(index >= 0 && index < length(), "out of bound");
  ProfileCacheClassChainEntry* entry = &_entries[index];
  entry->add_method_holder(mh);
}

void ProfileCacheClassChain::update_loaded_index(int index) {
  assert(index >= 0 && index < length(), "out of bound");
  while (index < length() && !_entries[index].is_not_loaded()) {
    index++;
  }
  set_loaded_index(index - 1);
}

static bool mark_skipped_if_redefined(ProfileCacheClassChain::ProfileCacheClassChainEntry* entry) {
  if (entry->contains_redefined_class()) {
    entry->set_skipped();
    return true;
  }
  return false;
}

static void enqueue_methods(ProfileCacheClassChain::ProfileCacheClassChainEntry* entry,
                            Stack<ProfileCacheMethodHold*, mtInternal>& compile_queue) {
  ProfileCacheMethodHold* mh = entry->method_holder();
  while (mh != NULL) {
    compile_queue.push(mh);
    mh = mh->next();
  }
}

void ProfileCacheClassChain::compile_methodholders_queue(Stack<ProfileCacheMethodHold*, mtInternal>& compile_queue) {
  while (!compile_queue.is_empty()) {
    ProfileCacheMethodHold* pmh = compile_queue.pop();
    compile_method(pmh);
    Thread* THREAD = Thread::current();
    if (HAS_PENDING_EXCEPTION) {
      ResourceMark rm;
      jprofilecache_log_warning(jprofilecache, "[JitProfileCache] WARNING: Exceptions happened in compiling %s",
                          pmh->method_name()->as_C_string());
      CLEAR_PENDING_EXCEPTION;
      continue;
    }
  }
}

void ProfileCacheClassChain::precompilation() {
  if (!try_transition_to_state(PROFILECACHE_COMPILING)) {
    jprofilecache_log_warning(jprofilecache, "JProfileCache [WARNING]: The compilation cannot be started in the current state");
    return;
  }

  const bool aggressive_mode = ProfileCacheAggressiveInit;
  jprofilecache_log_info(jprofilecache, "JProfileCache [INFO]: precompile mode=%s",
                         aggressive_mode ? "aggressive" : "conservative");
  if (aggressive_mode) {
    precompile_aggressive();
  } else {
    precompile_conservative();
  }
}

void ProfileCacheClassChain::precompile_conservative() {
  Thread* THREAD = Thread::current();
  bool cancel_precompilation = false;
  for (int index = 0; index < length(); index++) {
    if (cancel_precompilation) {
      break;
    }
    InstanceKlass* klass = NULL;
    Stack<ProfileCacheMethodHold*, mtInternal> compile_queue;
    {
      MutexLocker mu(ProfileCacheClassChain_lock);
      ProfileCacheClassChainEntry* entry = &_entries[index];
      switch (entry->class_state()) {
        case ProfileCacheClassChainEntry::_not_loaded:
          // Keep conservative mode consistent so index refresh can progress.
          entry->set_skipped();
        case ProfileCacheClassChainEntry::_load_skipped:
          break;
        case ProfileCacheClassChainEntry::_class_loaded:
          klass = entry->get_first_unlinked_klass();
          if (klass == NULL && entry->is_all_linked()) {
            entry->set_inited();
            if (!mark_skipped_if_redefined(entry)) {
              enqueue_methods(entry, compile_queue);
            }
          }
          break;
        case ProfileCacheClassChainEntry::_class_inited:
          if (!mark_skipped_if_redefined(entry)) {
            enqueue_methods(entry, compile_queue);
          }
          break;
        default:
          {
            ResourceMark rm;
            jprofilecache_log_error(jprofilecache, "[JitProfileCache] ERROR: class %s has an invalid state %d",
                              entry->class_name()->as_C_string(),
                              entry->class_state());
            return;
          }
      }
    }

    // Conservative mode is link-only: drive verify+prepare without proactive <clinit>.
    while (klass != NULL) {
      assert(THREAD->is_Java_thread(), "sanity check");
      klass->link_class(THREAD);
      if (HAS_PENDING_EXCEPTION) {
        Symbol *loader = JitProfileCacheUtils::get_class_loader_name(klass->class_loader_data());
        ResourceMark rm;
        jprofilecache_log_warning(jprofilecache,
                            "[JitProfileCache] WARNING: Exceptions happened in linking %s being loaded by %s",
                            klass->name()->as_C_string(), loader->as_C_string());
        CLEAR_PENDING_EXCEPTION;
        MutexLocker mu(ProfileCacheClassChain_lock);
        _entries[index].set_skipped();
        klass = NULL;
        break;
      }

      {
        MutexLocker mu(ProfileCacheClassChain_lock);
        ProfileCacheClassChainEntry* entry = &_entries[index];
        klass = entry->get_first_unlinked_klass();
        if (klass == NULL && entry->is_loaded() && entry->is_all_linked()) {
          entry->set_inited();
          if (!mark_skipped_if_redefined(entry)) {
            enqueue_methods(entry, compile_queue);
          }
        }
      }
    }

    {
      MutexLocker mu(ProfileCacheClassChain_lock);
      refresh_indexes();
      if (index > class_chain_inited_index()) {
        cancel_precompilation = true;
      }
    }

    // add method to compile queue and precompile
    compile_methodholders_queue(compile_queue);
  }
}

void ProfileCacheClassChain::precompile_aggressive() {
  Thread* THREAD = Thread::current();
  bool cancel_precompilation = false;
  int aggressive_upper_bound = length() - 1;
  int first_recorded_clinit_failure_index = -1;
  {
    MutexLocker mu(ProfileCacheClassChain_lock);
    for (int i = 0; i < length(); i++) {
      if (!_entries[i].recorded_clinit_succeeded()) {
        first_recorded_clinit_failure_index = i;
        aggressive_upper_bound = i - 1;
        break;
      }
    }
  }
  if (first_recorded_clinit_failure_index >= 0) {
    jprofilecache_log_info(jprofilecache,
                           "JProfileCache [INFO]: aggressive replay stops before first recorded <clinit> failure at index=%d (upper_bound=%d)",
                           first_recorded_clinit_failure_index, aggressive_upper_bound);
  } else {
    jprofilecache_log_info(jprofilecache,
                           "JProfileCache [INFO]: aggressive replay has no recorded <clinit> failure (upper_bound=%d)",
                           aggressive_upper_bound);
  }

  for (int index = 0; index < length(); index++) {
    if (index > aggressive_upper_bound) {
      jprofilecache_log_info(jprofilecache,
                             "JProfileCache [INFO]: aggressive replay reached configured stop index=%d",
                             aggressive_upper_bound);
      break;
    }
    if (cancel_precompilation) {
      break;
    }
    InstanceKlass* klass = NULL;
    Stack<ProfileCacheMethodHold*, mtInternal> compile_queue;
    {
      MutexLocker mu(ProfileCacheClassChain_lock);
      ProfileCacheClassChainEntry *entry = &_entries[index];
      switch(entry->class_state()) {
        case ProfileCacheClassChainEntry::_not_loaded:
          // if class not load before skip
          entry->set_skipped();
          {
            ResourceMark rm;
            char* class_name = entry->class_name()->as_C_string();
            char* class_loader_name = entry->class_loader_name()->as_C_string();
            char* class_path = entry->class_path()->as_C_string();
          }
        case ProfileCacheClassChainEntry::_load_skipped:
          break;
        case ProfileCacheClassChainEntry::_class_loaded:
          klass = entry->get_first_uninitialized_klass();
          entry->set_inited();
        case ProfileCacheClassChainEntry::_class_inited:
          if (!entry->contains_redefined_class()){
            ProfileCacheMethodHold* mh = entry->method_holder();
            while (mh != NULL) {
              compile_queue.push(mh);
              mh = mh->next();
            }
          }
          break;
        default:
          {
            ResourceMark rm;
            jprofilecache_log_error(jprofilecache, "[JitProfileCache] ERROR: class %s has an invalid state %d",
                              entry->class_name()->as_C_string(),
                              entry->class_state());
            return;
          }
      }
    }
    if (klass != NULL) {
      assert(THREAD->is_Java_thread(), "sanity check");
      klass->initialize(THREAD);
      if (HAS_PENDING_EXCEPTION) {
        Symbol *loader = JitProfileCacheUtils::get_class_loader_name(klass->class_loader_data());
        ResourceMark rm;
        jprofilecache_log_error(jprofilecache,
                                "[JitProfileCache] ERROR: Exceptions happened in initializing %s being loaded by %s",
                                klass->name()->as_C_string(), loader->as_C_string());
        return;
      }
    }
    {
      MutexLocker mu(ProfileCacheClassChain_lock);
      refresh_indexes();
      if (index > class_chain_inited_index()) {
        cancel_precompilation = true;
      }
    }

    // add method to compile queue and precompile
    compile_methodholders_queue(compile_queue);
  }
}

bool ProfileCacheClassChain::compile_method(ProfileCacheMethodHold* mh) {
  JavaThread* t = JavaThread::current();
  methodHandle m(t, mh->resolved_method());
  if (m() == NULL || m->compiled_by_jprofilecache() || m->has_compiled_code()) {
    return false;
  }

  InstanceKlass* klass = m->constants()->pool_holder();
  const int comp_level = mh->compile_level();
  if (!ProfileCacheAggressiveInit) {
    // Conservative mode only requires verify+prepare (linked).
    if (!klass->is_linked() || klass->is_in_error_state()) {
      return false;
    }
  } else {
    // Aggressive mode keeps initialized gate.
    if (!klass->is_initialized()) {
      return false;
    }
  }

  int bci = InvocationEntryBci;

  // commit compile
  bool ret = JitProfileCacheUtils::commit_compilation(m, comp_level, bci, t);
  if (ret) {
    m->set_compiled_by_jprofilecache(true);
    m->set_jpc_method_holder(mh);
    ResourceMark rm;
    jprofilecache_log_info(jprofilecache, "[JitProfileCache] method %s successfully compiled",
                     m->name_and_sig_as_C_string());
  }
  return ret;
}

void ProfileCacheClassChain::refresh_indexes() {
  assert_lock_strong(ProfileCacheClassChain_lock);
  int loaded = loaded_index();
  int inited = class_chain_inited_index();
  const bool use_linked_gate = !ProfileCacheAggressiveInit;
  for (int i = inited + 1; i < length(); i++) {
    ProfileCacheClassChainEntry* e = &_entries[i];
    int len = e->resolved_klasses()->length();
    if (e->is_not_loaded()) {
      assert(len == 0, "wrong state");
    }
    if (e->is_loaded()) {
      assert(len > 0, "class init chain entry state error");
      if (use_linked_gate ? e->is_all_linked() : e->is_all_initialized()) {
        e->set_inited();
      }
    }
    if (e->is_loaded() && i == loaded + 1) {
      loaded = i;
    } else if (e->is_inited() && i == inited + 1) {
      loaded = i;
      inited = i;
    } else if (e->is_skipped()) {
      if (i == loaded + 1) {
        loaded = i;
      }
      if (i == inited + 1) {
        inited = i;
      }
    } else {
      break;
    }
  }
  assert(loaded >= inited, "loaded index must not less than inited index");
  set_loaded_index(loaded);
  set_inited_index(inited);
}

void ProfileCacheClassChain::unload_class() {
  unload_class(NULL);
}

void ProfileCacheClassChain::unload_class(BoolObjectClosure* is_alive_closure) {
  assert(SafepointSynchronize::is_at_safepoint(), "must be in safepoint");
  AlwaysTrueClosure always_true;
  if (is_alive_closure == NULL) {
    is_alive_closure = &always_true;
  }
  for (int i = 0; i < length(); i++) {
    ProfileCacheClassChainEntry* entry = this->at(i);
    GrowableArray<InstanceKlass*>* array = entry->resolved_klasses();
    GrowableArray<jobject>* keep_array = entry->method_keep_holders();
    for (int i = 0; i < array->length(); i++) {
      InstanceKlass* k = array->at(i);
      if (k == NULL) {
        continue;
      }

      // if class not load continue
      if (entry->is_not_loaded() || entry->is_skipped()) {
        continue;
      }

      ClassLoaderData* data = k->class_loader_data();
      // if data is NULL or not alive should be remove
      if (data == NULL || !data->is_alive(is_alive_closure)) {
        // remove class from chain
        array->remove_at(i);
        JNIHandles::destroy_global(keep_array->at(i));
        keep_array->remove_at(i);
      }
    }
    for (ProfileCacheMethodHold* holder = entry->method_holder(); holder != NULL;
         holder = holder->next()) {
      // if method not compile or deopted continue
      if (holder->resolved_method() == NULL) {
        continue;
      }
      if (!holder->is_alive(is_alive_closure)) {
        // process the method in the class.
        holder->set_resolved_method(NULL);
      }
    }
  }
}

ProfileCacheMethodHold* ProfileCacheClassChain::resolve_method_info(Method* method, ProfileCacheClassHolder* holder) {
  ProfileCacheMethodHold* mh = NULL;
  // find method
  for (int i = 0; i < holder->method_list()->length(); i++) {
    ProfileCacheMethodHold* current_mh = holder->method_list()->at(i);
    if (current_mh->is_method_match(method)) {
      mh = current_mh;
      break;
    }
  }
  if (mh == NULL) {
    return mh;
  } else if (mh->resolved_method() == NULL) {
    mh->set_resolved_method(method);
    method->set_jpc_method_holder(mh);
    return mh;
  } else {
    jprofilecache_log_info(jprofilecache, "[JitProfileCache] method %s is resolved again",
                            method->name_and_sig_as_C_string());
    return mh;
  }
}

void ProfileCacheClassChain::preload_class_in_constantpool() {
  int index = 0;
  int klass_index = 0;
  while (true) {
    InstanceKlass* current_k = NULL;
    {
      MutexLocker mu(ProfileCacheClassChain_lock);
      if (index == length()) {
        break;
      }
      ProfileCacheClassChain::ProfileCacheClassChainEntry* e = this->at(index);
      GrowableArray<InstanceKlass*>* array =  e->resolved_klasses();
      assert(array != NULL, "should not be NULL");
      if (e->is_skipped() || e->is_not_loaded() || klass_index >= array->length()) {
        index++;
        klass_index = 0;
        continue;
      }
      current_k = array->at(klass_index);
    }

    if (current_k != NULL) {
      current_k->constants()->preload_jprofilecache_classes(JavaThread::current());
      jprofilecache_log_info(jprofilecache, "[JitProfileCache] class %s is preloaded",
                               current_k->internal_name());
    }
    klass_index++;
  }
}
