/*
* Copyright (c) 2025, Huawei Technologies Co., Ltd. All rights reserved.
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

#include "classfile/classLoaderData.hpp"
#include "classfile/classLoaderData.inline.hpp"
#include "classfile/symbolTable.hpp"
#include "classfile/systemDictionary.hpp"
#include "compiler/compileBroker.hpp"
#include "jprofilecache/jitProfileCache.hpp"
#include "jprofilecache/jitProfileRecord.hpp"
#include "jprofilecache/jitProfileCacheThread.hpp"
#include "jprofilecache/jitProfileCacheLogParser.hpp"
#include "oops/method.hpp"
#include "oops/typeArrayKlass.hpp"
#include "runtime/arguments.hpp"
#include "runtime/compilationPolicy.hpp"
#include "runtime/fieldType.hpp"
#include "runtime/handles.inline.hpp"
#include "runtime/javaCalls.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/os.hpp"
#include "runtime/thread.hpp"
#include "utilities/hashtable.inline.hpp"
#include "utilities/stack.hpp"
#include "utilities/stack.inline.hpp"
#include "runtime/atomic.hpp"
#include "jprofilecache/jitProfileCacheLog.hpp"  // must be last one to use customized jprofilecache log
#include "libadt/dict.hpp"

#define JITPROFILECACHE_VERSION  0x1

JitProfileCache*                JitProfileCache::_jit_profile_cache_instance         = NULL;

JitProfileCache::JitProfileCache()
  : _jit_profile_cache_state(NOT_INIT),
    _jit_profile_cache_info(NULL),
    _jit_profile_cache_recorder(NULL),
    _excluding_matcher(NULL),
    _jit_profile_cache_version(JITPROFILECACHE_VERSION),
    _dummy_method(NULL),
    profilecacheComplete(false) {
}

JitProfileCache::~JitProfileCache() {
  delete _jit_profile_cache_recorder;
  delete _jit_profile_cache_info;
}

JitProfileCache* JitProfileCache::create_instance() {
  _jit_profile_cache_instance = new JitProfileCache();
  return _jit_profile_cache_instance;
}

JitProfileCache::JitProfileCacheState JitProfileCache::init_for_recording() {
  if (!(JProfilingCacheRecording && !JProfilingCacheCompileAdvance)) {
    jprofilecache_log_error(profilecache)("[JitProfileCache] ERROR: JitProfileCache option verify failure");
    _jit_profile_cache_state = JitProfileCache::IS_ERR;
    return _jit_profile_cache_state;
  }
  _jit_profile_cache_recorder = new JitProfileRecorder();
  _jit_profile_cache_recorder->set_holder(this);
  _jit_profile_cache_recorder->init();

  // wait JProfilingCacheRecordTime flush jit recorder
  if (JProfilingCacheRecordTime > 0) {
    JitProfileCacheThread::launch_with_delay(JProfilingCacheRecordTime);
  }

  // check state
  if (_jit_profile_cache_recorder->is_valid()) {
    _jit_profile_cache_state = JitProfileCache::IS_OK;
  } else  {
    _jit_profile_cache_state = JitProfileCache::IS_ERR;
  }
  return _jit_profile_cache_state;
}

JitProfileCache::JitProfileCacheState JitProfileCache::init_for_profilecache() {
  if (!(!JProfilingCacheRecording && JProfilingCacheCompileAdvance)) {
    jprofilecache_log_error(profilecache)("[JitProfileCache] ERROR : JitProfile option verify fail");
    _jit_profile_cache_state = JitProfileCache::IS_ERR;
    return _jit_profile_cache_state;
  }
  if (CompilationProfileCacheExclude != NULL) {
    _excluding_matcher = new (ResourceObj::C_HEAP, mtClass) SymbolRegexMatcher<mtClass>(CompilationProfileCacheExclude);
  }
  if (CompilationProfileCacheExplicitDeopt && JProfilingCacheDeoptTime > 0) {
    jprofilecache_log_warning(profilecache)("[JitProfileCache] WARNING : JProfilingCacheDeoptTime is unused when CompilationProfileCacheExplicitDeopt is enable");
  }
  _jit_profile_cache_info = new JitProfileCacheInfo();
  _jit_profile_cache_info->set_holder(this);
  _jit_profile_cache_info->init();
  if (_jit_profile_cache_info->is_valid()) {
      _jit_profile_cache_state = JitProfileCache::IS_OK;
  } else  {
      _jit_profile_cache_state = JitProfileCache::IS_ERR;
  }
  return _jit_profile_cache_state;
}

void JitProfileCache::init() {

#if defined(__aarch64__)
  if (!VM_Version::is_hisi_enabled()) {
    if (JProfilingCacheCompileAdvance || JProfilingCacheRecording) {
       tty->print_cr("JProfileCache is only supported on Kunpeng architecture. ");
       vm_exit(-1);
    }
    return;
  }
#else
  // x86 return
  if (JProfilingCacheCompileAdvance || JProfilingCacheRecording) {
    tty->print_cr("JProfileCache is only supported on Kunpeng architecture. ");
    vm_exit(-1);
  }
  return;
#endif

  // set log level
  set_log_level();

  if (JProfilingCacheCompileAdvance) {
    init_for_profilecache();
  } else if(JProfilingCacheRecording) {
    init_for_recording();
  }
  if ((JProfilingCacheRecording || JProfilingCacheCompileAdvance) && !JitProfileCache::is_valid()) {
    jprofilecache_log_error(profilecache)("[JitProfileCache] ERROR: JProfileCache init error.");
    vm_exit(-1);
  }
}

void JitProfileCache::set_log_level() {
  if (ProfilingCacheLogLevel == NULL) {
    jprofilecache_log_error(profilecache)("[JitProfileCache] Error: ProfilingCacheLogLevel is invalid must in trace, debug, info, warning, error, off");
    _jit_profile_cache_state = JitProfileCache::IS_ERR;
    vm_exit(-1);
  } else if (strcmp(ProfilingCacheLogLevel, "trace") == 0) {
    LogLevel::LogLevelNum = LogLevel::Trace;
  } else if (strcmp(ProfilingCacheLogLevel, "debug") == 0) {
    LogLevel::LogLevelNum = LogLevel::Debug;
  } else if (strcmp(ProfilingCacheLogLevel, "info") == 0) {
    LogLevel::LogLevelNum = LogLevel::Info;
  } else if (strcmp(ProfilingCacheLogLevel, "warning") == 0) {
    LogLevel::LogLevelNum = LogLevel::Warning;
  } else if (strcmp(ProfilingCacheLogLevel, "error") == 0) {
    LogLevel::LogLevelNum = LogLevel::Error;
  } else if (strcmp(ProfilingCacheLogLevel, "off") == 0) {
    LogLevel::LogLevelNum = LogLevel::Off;
  } else {
    jprofilecache_log_error(profilecache)("[JitProfileCache] Error: ProfilingCacheLogLevel is invalid must in trace, debug, info, warning, error, off");
    _jit_profile_cache_state = JitProfileCache::IS_ERR;
    vm_exit(-1);
  }
}

JitProfileCache::JitProfileCacheState JitProfileCache::flush_recorder() {
  if(_jit_profile_cache_state == IS_ERR) {
    return _jit_profile_cache_state;
  }
  _jit_profile_cache_recorder->flush_record();
  if (_jit_profile_cache_recorder->is_valid()) {
    _jit_profile_cache_state = IS_OK;
  } else {
    _jit_profile_cache_state = IS_ERR;
  }
  return _jit_profile_cache_state;
}

bool JitProfileCache::commit_compilation(methodHandle m, int bci, TRAPS) {
    int comp_level = CompLevel_full_optimization;
    if (CompilationPolicy::can_be_compiled(m, comp_level)) {
        CompileBroker::compile_method(m, bci, comp_level,
                                      methodHandle(), 0,
                                      "JitProfileCache", THREAD);
        return true;
    }
    return false;
}

Symbol* JitProfileCache::get_class_loader_name(ClassLoaderData* cld) {
  Handle class_loader(Thread::current(), cld->class_loader());
  Symbol* loader_name = NULL;
  if (class_loader() != NULL) {
    loader_name = JitProfileCacheInfo::remove_meaningless_suffix(class_loader()->klass()->name());
  } else {
    loader_name = SymbolTable::new_symbol("NULL", Thread::current());
  }
  return loader_name;
}

#define HEADER_SIZE              36
#define MAGIC_NUMBER             0xBABA

#define JVM_DEFINE_CLASS_PATH "_JVM_DefineClass_"

JProfileCacheClassDictionary::JProfileCacheClassDictionary(int size)
  : Hashtable<Symbol*, mtInternal>(size, sizeof(ProfileCacheClassEntry)) {
}

JProfileCacheClassDictionary::~JProfileCacheClassDictionary() { }

ProfileCacheClassEntry* JProfileCacheClassDictionary::new_entry(Symbol* symbol) {
  unsigned int hash = symbol->identity_hash();
  ProfileCacheClassEntry* entry = (ProfileCacheClassEntry*)Hashtable<Symbol*, mtInternal>::
                             new_entry(hash, symbol);
  entry->init();
  return entry;
}

ProfileCacheClassEntry* JProfileCacheClassDictionary::find_entry(InstanceKlass* k) {
  Symbol* name = k->name();
  Symbol* path = k->source_file_path();
  if (path == NULL) {
    path = SymbolTable::new_symbol(JVM_DEFINE_CLASS_PATH, Thread::current());
  }
  Symbol* loader_name = JitProfileCache::get_class_loader_name(k->class_loader_data());
  int hash = name->identity_hash();
  return find_entry(hash, name, loader_name, path);
}

ProfileCacheClassEntry* JProfileCacheClassDictionary::find_entry(unsigned int hash_value,
                                                      Symbol* name,
                                                      Symbol* loader_name,
                                                      Symbol* path) {
  int index = hash_to_index(hash_value);
  for (ProfileCacheClassEntry* p = bucket(index); p != NULL; p = p->next()) {
    if (p->literal()->fast_compare(name) == 0 &&
        p->class_loader_name()->fast_compare(loader_name) == 0 &&
        p->class_path()->fast_compare(path) == 0) {
      return p;
    }
  }
  return NULL;
}

ProfileCacheClassEntry* JProfileCacheClassDictionary::find_head_entry(unsigned int hash_value,
                                                           Symbol* name) {
  int index = hash_to_index(hash_value);
  for (ProfileCacheClassEntry* p = bucket(index); p != NULL; p = p->next()) {
    if (p->literal()->fast_compare(name) == 0) {
      return p;
    }
  }
  return NULL;
}

ProfileCacheClassHolder* ProfileCacheClassEntry::find_class_holder(unsigned int size,
                                                            unsigned int crc32) {
  for (ProfileCacheClassHolder* p = this->head_holder(); p != NULL; p = p->next()) {
    if (p->crc32() == crc32 && p->size() == size) {
      return p;
    }
  }
  return NULL;
}

ProfileCacheMethodHold* ProfileCacheMethodHold::clone_and_add() {
  ProfileCacheMethodHold* clone = new ProfileCacheMethodHold(*this);
  clone->set_next(_next);
  _next = clone;
  return clone;
}

ProfileCacheClassEntry* JProfileCacheClassDictionary::find_or_create_class_entry(unsigned int hash_value,
                                                                    Symbol* name,
                                                                    Symbol* loader_name,
                                                                    Symbol* path,
                                                                    int index) {
  ProfileCacheClassEntry* p = find_entry(hash_value, name, loader_name, path);
  if (p == NULL) {
    p = new_entry(name);
    p->set_chain_offset(index);
    p->set_class_loader_name(loader_name);
    p->set_class_path(path);
    add_entry(hash_to_index(hash_value), p);
  }
  return p;
}

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
    _owns_method_list(true),
    _is_method_deopted(false),
    _next(NULL),
    _resolved_method(NULL),
    _method_list(new (ResourceObj::C_HEAP, mtClass)
             GrowableArray<BytecodeProfileRecord*>(METHOD_LIST_INITIAL_CAPACITY, true, mtClass)) {
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
    _owns_method_list(false),
    _is_method_deopted(false),
    _next(NULL),
    _resolved_method(NULL),
    _method_list(rhs._method_list) {
}

ProfileCacheMethodHold::~ProfileCacheMethodHold() {
  if (_owns_method_list) {
    delete _method_list;
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

bool ProfileCacheMethodHold::is_alive(BoolObjectClosure* is_alive_closure) const {
  if (_resolved_method == NULL || _resolved_method->constMethod() == NULL || _resolved_method->constants() == NULL || _resolved_method->constants()->pool_holder() == NULL) {
    return false;
  }
  ClassLoaderData* data = _resolved_method->method_holder()->class_loader_data();
  if (data == NULL || !data->is_alive(is_alive_closure)) {
    return false;
  }
  return true;
}

#define CLASS_METHOD_LIST_INITIAL_CAPACITY  16

ProfileCacheClassHolder::ProfileCacheClassHolder(Symbol* name, Symbol* loader_name,
                                       Symbol* path, unsigned int size,
                                       unsigned int hash, unsigned int crc32)
  : _class_size(size),
    _class_hash(hash),
    _class_crc32(crc32),
    _class_name(name),
    _class_loader_name(loader_name),
    _class_path(path),
    _class_method_list(new (ResourceObj::C_HEAP, mtInternal)
                 GrowableArray<ProfileCacheMethodHold*>(CLASS_METHOD_LIST_INITIAL_CAPACITY, true, mtClass)),
    _class_resolved(false),
    _next(NULL) {
}


ProfileCacheClassHolder::~ProfileCacheClassHolder() {
  delete _class_method_list;
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

bool ProfileCacheClassChain::ProfileCacheClassChainEntry::contains_redefined_class() {
  int len = resolved_klasses()->length();
  for (int i = 0; i < len; i++) {
    InstanceKlass* k = resolved_klasses()->at(i);
    if (k != NULL && k->has_been_redefined()) {
      ResourceMark rm;
      jprofilecache_log_warning(profilecache)("[JitProfileCache] WARNING: ignore redefined class after API"
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

ProfileCacheClassChain::ProfileCacheClassChain(unsigned int size)
  : _class_chain_inited_index(-1),
    _loaded_class_index(-1),
    _length(size),
    _state(NOT_INITED),
    _entries(new ProfileCacheClassChainEntry[size]),
    _holder(NULL),
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
    case PROFILECACHE_PRE_DEOPTIMIZE:
      return "trigger deoptimize";
    case PROFILECACHE_DEOPTIMIZING:
      return "deoptmizing";
    case PROFILECACHE_DEOPTIMIZED:
      return "deoptimize done";
    case PROFILECACHE_ERROR_STATE:
      return "profilecache error state";
  }
  assert(false, "invalid state");
  return NULL;
}

bool ProfileCacheClassChain::try_transition_to_state(ClassChainState new_state) {
  ClassChainState old_state = current_state();
  if (old_state == new_state) {
    jprofilecache_log_warning(profilecache)("JProfileCache [WARNING]: profilecache state has already been %s Doesn't need transferred to %s", 
                                            get_state(old_state), get_state(new_state));
    return true;
  }
  bool can_transfer = false;
  switch (new_state) {
    case PROFILECACHE_ERROR_STATE:
      if (old_state != PROFILECACHE_DEOPTIMIZED) {
        can_transfer = true;
      }
      break;
    default:
      if (new_state == old_state + 1) {
        can_transfer = true;
      }
      break;
  }
  if (can_transfer) {
    if (Atomic::cmpxchg((jint)new_state, (jint*)&_state, (jint)old_state) == old_state) {
      return true;
    } else {
      jprofilecache_log_warning(profilecache)("JProfileCache [WARNING]: failed to transfer profilecache state from %s to %s, conflict with other operation", 
                                              get_state(old_state), get_state(new_state));
      return false;
    }
  } else {
    jprofilecache_log_warning(profilecache)("JProfileCache [WARNING]: can not transfer profilecache state from %s to %s", 
                                            get_state(old_state), get_state(new_state));
    return false;
  }
}

void ProfileCacheClassChain::mark_loaded_class(InstanceKlass* k) {
  Symbol* class_name = k->name();
  unsigned int crc32 = k->crc32();
  unsigned int size = k->bytes_size();

  if (!can_record_class()) {
    return;
  }

  ProfileCacheClassEntry* class_entry = holder()->jit_profile_cache_dict()->find_entry(k);
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
    jprofilecache_log_debug(profilecache)("[JitProfileCache] DEBUG : class %s is not in proFile",
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
    jprofilecache_log_warning(profilecache)("[JitProfileCache] WARNING : Duplicate load class %s at index %d",
                        k->name()->as_C_string(), chain_index);
  }
}

void ProfileCacheClassChain::resolve_class_methods(InstanceKlass* k, ProfileCacheClassHolder* holder, int chain_index) {
  MutexLockerEx mu(ProfileCacheClassChain_lock);
  int methods = k->methods()->length();
  for (int index = 0; index < methods; index++) {
    Method* m = k->methods()->at(index);
    resolve_method_info(m, holder);
  }
  {
    ResourceMark rm;
    jprofilecache_log_debug(profilecache)("[JitProfileCache] DEBUG : class %s at index %d method_list has bean recorded",
                      k->name()->as_C_string(), chain_index);
  }
  holder->set_resolved();
}

void ProfileCacheClassChain::update_class_chain (InstanceKlass* k, int chain_index) {
  MutexLockerEx mu(ProfileCacheClassChain_lock);
  assert(chain_index >= 0 && chain_index <= length(), "index out of bound");
  assert(loaded_index() >= class_chain_inited_index(), "loaded index must larger than inited index");
  ProfileCacheClassChainEntry* chain_entry = &_entries[chain_index];

  // check class state is skip or init return
  if (chain_entry->is_skipped()) {
    ResourceMark rm;
    char* class_name = k->name()->as_C_string();
    int index = chain_index;
    bool print_log_detail = false;
    if (LogLevel::Warning >= LogLevel::LogLevelNum) {
      print_log_detail = true;
    }
    os::Linux::handle_ignore_class(class_name, index, print_log_detail);
    return;
  } else if (chain_entry->is_inited()) {
    return;
  }
  // set class reserved
  chain_entry->resolved_klasses()->append(k);
  Thread* thread = Thread::current();
  chain_entry->method_keep_holders()->append(JNIHandles::make_global(Handle(thread, k->klass_holder())));

  int status = os::Linux::get_class_state();
  chain_entry->set_class_state(status);

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

void ProfileCacheClassChain::compile_methodholders_queue(Stack<ProfileCacheMethodHold*, mtInternal>& compile_queue) {
  while (!compile_queue.is_empty()) {
    ProfileCacheMethodHold* pmh = compile_queue.pop();
    compile_method(pmh);
    Thread* THREAD = Thread::current();
    if (HAS_PENDING_EXCEPTION) {
      ResourceMark rm;
      jprofilecache_log_warning(profilecache)("[JitProfileCache] WARNING: Exceptions happened in compiling %s",
                          pmh->method_name()->as_C_string());
      CLEAR_PENDING_EXCEPTION;
      continue;
    }
  }
}

void ProfileCacheClassChain::precompilation() {
  Thread* THREAD = Thread::current();
  if (!try_transition_to_state(PROFILECACHE_COMPILING)) {
    jprofilecache_log_warning(profilecache)("JProfileCache [WARNING]: The compilation cannot be started in the current state");
    return;
  }

  bool cancel_precompilation = false;
  for ( int index = 0; index < length(); index++ ) {
    if (cancel_precompilation) {
      break;
    }
    InstanceKlass* klass = NULL;
    Stack<ProfileCacheMethodHold*, mtInternal> compile_queue;
    {
      MutexLockerEx mu(ProfileCacheClassChain_lock);
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
            bool print_log_detail = false;
            if (LogLevel::Warning >= LogLevel::LogLevelNum) {
              print_log_detail = true;
            }
            os::Linux::handle_skipped(class_name, class_loader_name, class_path, print_log_detail);
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
            jprofilecache_log_error(profilecache)("[JitProfileCache] ERROR: class %s has an invalid state %d",
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
        Symbol *loader = JitProfileCache::get_class_loader_name(klass->class_loader_data());
        ResourceMark rm;
        jprofilecache_log_error(profilecache)("[JitProfileCache] ERROR: Exceptions happened in initializing %s being loaded by %s",
                          klass->name()->as_C_string(), loader->as_C_string());
        return;
      }
    }
    {
      MutexLockerEx mu(ProfileCacheClassChain_lock);
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
  Thread* t = Thread::current();
  methodHandle m(t, mh->resolved_method());
  if (m() == NULL || m->compiled_by_jprofilecache() || m->has_compiled_code()) {
    return false;
  }
  InstanceKlass* klass = m->constants()->pool_holder();

  // if klass not initialize return
  if (!klass->is_initialized()) {
    return false;
  }

  m->set_compiled_by_jprofilecache(true);
  int bci = InvocationEntryBci;

  // commit compile
  bool ret = JitProfileCache::commit_compilation(m, bci, t);
  if (ret) {
    ResourceMark rm;
    jprofilecache_log_info(profilecache)("[JitProfileCache] method %s successfully compiled",
                     m->name_and_sig_as_C_string());
  }
  return ret;
}

void ProfileCacheClassChain::refresh_indexes() {
  assert_lock_strong(ProfileCacheClassChain_lock);
  int loaded = loaded_index();
  int inited = class_chain_inited_index();
  for (int i = inited + 1; i < length(); i++) {
    ProfileCacheClassChainEntry* e = &_entries[i];
    int len = e->resolved_klasses()->length();
    if (e->is_not_loaded()) {
      assert(len == 0, "wrong state");
    }
    if (e->is_loaded()) {
      assert(len > 0, "class init chain entry state error");
      if (e->is_all_initialized()) {
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

bool ProfileCacheClassChain::should_deoptimize_methods() {
  assert(JProfilingCacheCompileAdvance, "Sanity check");
  assert(SafepointSynchronize::is_at_safepoint(), "must be in safepoint");
  ClassChainState state = current_state();
  if (state == PROFILECACHE_DEOPTIMIZED || state == PROFILECACHE_ERROR_STATE) {
    return false;
  }
  if (!CompilationProfileCacheExplicitDeopt && JProfilingCacheDeoptTime > 0) {
    if (_init_timestamp.seconds() < JProfilingCacheDeoptTime) {
      return false;
    } else if (state ==PROFILECACHE_DONE) {
      try_transition_to_state(PROFILECACHE_PRE_DEOPTIMIZE);
    } else {
    }
  }

  if (current_state() != PROFILECACHE_DEOPTIMIZING
      && current_state() != PROFILECACHE_PRE_DEOPTIMIZE) {
    return false;
  }

  Method* dummy_method = JitProfileCache::instance()->dummy_method();
  if (dummy_method == NULL || dummy_method->code() == NULL) {
    return false;
  }

  if (_last_timestamp.seconds() < CompilationProfileCacheDeoptMinInterval) {
    return false;
  }
  VM_Operation* op = VMThread::vm_operation();
  if (op != NULL && !op->allow_nested_vm_operations()) {
    return false;
  }
  if (_length <= 1) {
    return false;
  }
  return true;
}

void ProfileCacheClassChain::deopt_prologue() {
  if (current_state() == PROFILECACHE_PRE_DEOPTIMIZE) {
    if (try_transition_to_state(PROFILECACHE_DEOPTIMIZING)) {
      jprofilecache_log_info(profilecache)("JProfileCache [INFO]: start deoptimize profilecache methods");
      _deopt_index = length() - 1;
      while (_deopt_index > 0 && _deopt_cur_holder == NULL) {
        ProfileCacheClassChain::ProfileCacheClassChainEntry* entry = this->at(_deopt_index);
        _deopt_cur_holder = entry->method_holder();
        _deopt_index--;
      }
    } else {
      ShouldNotReachHere();
    }
  } else {
    guarantee(current_state() == PROFILECACHE_DEOPTIMIZING, "invalid profilecache state");
  }
}

void ProfileCacheClassChain::deopt_epilogue() {
  try_transition_to_state(PROFILECACHE_DEOPTIMIZED);
  jprofilecache_log_info(profilecache)("JProfileCache [INFO]: all profilecache methods have been deoptimized");
  // free all keep alive method
  for (int i = 0; i < length(); i++) {
    ProfileCacheClassChainEntry *entry = this->at(i);
    GrowableArray<InstanceKlass *> *array = entry->resolved_klasses();
    if (!entry->method_keep_holders()->is_empty()) {
      int len = entry->method_keep_holders()->length();
      for (int i = 0; i < len; i++) {
        JNIHandles::destroy_global(entry->method_keep_holders()->at(i));
      }
    }
  }
}

void ProfileCacheClassChain::invoke_deoptimize_vmop() {
  VM_Deoptimize op;
  VMThread::execute(&op);
}

void ProfileCacheClassChain::deoptimize_methods() {
  assert(SafepointSynchronize::is_at_safepoint(), "profilecache deoptimize methods must be in safepoint");
  deopt_prologue();

  Method* dummy_method = JitProfileCache::instance()->dummy_method();
  assert( dummy_method != NULL && dummy_method->code() != NULL, "profilecache the dummy method must be compiled");
  int dummy_compile_id = dummy_method->code()->compile_id();

  MethodHolderIterator iter(this, _deopt_cur_holder, _deopt_index);
  int num = 0;
  while (*iter != NULL) {
    ProfileCacheMethodHold* pmh = *iter;
    if (pmh->resolved_method() == NULL) {
      iter.next();
      continue;
    }
    methodHandle m(pmh->resolved_method());

    if(m() == NULL || !m->compiled_by_jprofilecache()) {
      iter.next();
      continue;
    }
#ifndef PRODUCT
    m->set_deopted_by_jprofilecache(true);
#endif
    pmh->set_is_method_deopted(true);
    if (m->code() != NULL && m->code()->compile_id() > dummy_compile_id) {
      ResourceMark rm;
      jprofilecache_log_warning(profilecache)("[JitProfileCache] WARNING : skip deoptimize %s because it is compiled after precompile",
                          m->name_and_sig_as_C_string());
      iter.next();
      continue;
    }
    int result = 0;
    if (m->code() != NULL) {
      m->code()->mark_for_deoptimization();
      result++;
    }
    result += CodeCache::mark_for_deoptimization(m());
    if (result > 0) {
      ResourceMark rm;
      jprofilecache_log_warning(profilecache)("[JitProfileCache] WARNING : deoptimize precompile method %s",
                          m->name_and_sig_as_C_string());
      num++;
    }
    iter.next();
    if (num == (int)CompilationProfileCacheDeoptNumOfMethodsPerIter) {
      break;
    }
  }
  if (num > 0) {
    invoke_deoptimize_vmop();
  }

  _last_timestamp.update();
  _deopt_index = iter.index();
  _deopt_cur_holder = *iter;

  if (*iter == NULL) {
    deopt_epilogue();
  }
}

void ProfileCacheClassChain::unload_class(BoolObjectClosure *is_alive) {
  assert(SafepointSynchronize::is_at_safepoint(), "must be in safepoint");
  if (deopt_has_done()) {
    return;
  }
  for (int i = 0; i < length(); i++) {
    ProfileCacheClassChainEntry* entry = this->at(i);
    GrowableArray<InstanceKlass*>* array = entry->resolved_klasses();
    // Check whether the keep alive method should be unloading.
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
      if (data == NULL || !data->is_alive(is_alive)) {
        // remove class from chain
        array->remove_at(i);
        JNIHandles::destroy_global(keep_array->at(i));
        keep_array->remove_at(i);
      }
    }
    for (ProfileCacheMethodHold* holder = entry->method_holder(); holder != NULL;
         holder = holder->next()) {
      // if method not compile or deopted continue
      if (holder->is_method_deopted() || holder->resolved_method() == NULL) {
        continue;
      }
      if (!holder->is_alive(is_alive)) {
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
    return mh;
  } else {
    ProfileCacheMethodHold* new_holder = mh->clone_and_add();
    new_holder->set_resolved_method(method);
    return new_holder;
  }
}

#define PRELOAD_CLASS_HS_SIZE    10240

JitProfileCacheInfo::JitProfileCacheInfo()
  : _jit_profile_cache_dict(NULL),
    _profile_cache_chain(NULL),
    _method_loaded_count(0),
    _state(NOT_INIT),
    _holder(NULL),
    _jvm_booted_is_done(false) {
}

JitProfileCacheInfo::~JitProfileCacheInfo() {
  delete _jit_profile_cache_dict;
  delete _profile_cache_chain;
}

Symbol* JitProfileCacheInfo::remove_meaningless_suffix(Symbol* s) {
  ResourceMark rm;
  Thread* t = Thread::current();
  Symbol* result = s;
  char* s_char = s->as_C_string();
  int len = (int)::strlen(s_char);
  int i = 0;
  for (i = 0; i < len - 1; i++) {
    if (s_char[i] == '$' && s_char[i+1] == '$') {
      break;
    }
  }
  if (i < len - 1) {
    i = i == 0 ? i = 1: i;
    result = SymbolTable::new_symbol(s_char, i, t);
    s_char = result->as_C_string();
  }
  len = (int)::strlen(s_char);
  i = len - 1;
  for (; i >= 0; i--) {
    if (s_char[i] >= '0' && s_char[i] <= '9') {
      continue;
    } else if (s_char[i] == '$') {
      continue;
    } else {
      break;
    }
  }
  if (i != len - 1){
    i = i == -1 ? 0 : i;
    result = SymbolTable::new_symbol(s_char, i + 1, t);
  }
  return result;
}

void JitProfileCacheInfo::jvm_booted_is_done() {
  _jvm_booted_is_done = true;
  ProfileCacheClassChain* chain = this->chain();
  assert(chain != NULL, "ProfileCacheClassChain is NULL");
}

void ProfileCacheClassChain::preload_class_in_constantpool() {
  int index = 0;
  int klass_index = 0;
  while (true) {
    InstanceKlass* current_k = NULL;
    {
      MutexLockerEx mu(ProfileCacheClassChain_lock);
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
      current_k->constants()->preload_jprofilecache_classes(Thread::current());
    }
    klass_index++;
  }
}

void JitProfileCacheInfo::notify_precompilation() {
  ProfileCacheClassChain *chain = this->chain();
  assert(chain != NULL, "ProfileCacheClassChain is NULL");
  chain->try_transition_to_state(ProfileCacheClassChain::PRE_PROFILECACHE);

  // preload class
  jprofilecache_log_info(profilecache)("JProfileCache [INFO]: start preload class from constant pool");
  chain->preload_class_in_constantpool();

  // precompile cache method
  jprofilecache_log_info(profilecache)("JProfileCache [INFO]: start profilecache compilation");
  chain->precompilation();
  Thread *THREAD = Thread::current();
  if (HAS_PENDING_EXCEPTION) {
    return;
  }

  JitProfileCache *jpc = this->holder();
  Method *dm = jpc->dummy_method();
  if (dm != NULL) {
    guarantee(dm->code() == NULL, "dummy method has been compiled unexceptedly!");
    methodHandle mh(THREAD, dm);
    JitProfileCache::commit_compilation(mh, InvocationEntryBci, THREAD);
  }
  if (!chain->try_transition_to_state(ProfileCacheClassChain::PROFILECACHE_DONE)) {
    jprofilecache_log_error(profilecache)("JProfileCache [ERROR]: can not change state to PROFILECACHE_DONE");
  } else {
    jprofilecache_log_info(profilecache)("JProfileCache [INFO]: profilecache compilation is done");
  }
}

bool JitProfileCacheInfo::should_preload_class(Symbol* s) {
  SymbolRegexMatcher<mtClass>* matcher = holder()->excluding_matcher();
  if (matcher != NULL && matcher->matches(s)) {
    return false;
  }
  int hash = s->identity_hash();
  ProfileCacheClassEntry* e = jit_profile_cache_dict()->find_head_entry(hash, s);
  if (e == NULL) {
    return false;
  }
  if (!CompilationProfileCacheResolveClassEagerly) {
    int offset = e->chain_offset();
    ProfileCacheClassChain::ProfileCacheClassChainEntry* entry = chain()->at(offset);
    return entry->is_not_loaded();
  } else {
    return true;
  }
}

bool JitProfileCacheInfo::resolve_loaded_klass(InstanceKlass* k) {
  if (k == NULL) { return false; }
  if (k->is_jprofilecache_recorded()) {
    return false;
  }
  {
    MutexLockerEx mu(ProfileCacheClassChain_lock);
    if (!chain()->can_record_class()) {
      return false;
    }
  }
  k->set_jprofilecache_recorded(true);
  chain()->mark_loaded_class(k);
  return true;
}

class RandomFileStreamGuard : StackObj {
public:
  RandomFileStreamGuard(randomAccessFileStream* fs)
    : _file_stream(fs) {
  }

  ~RandomFileStreamGuard() { delete _file_stream; }

  randomAccessFileStream* operator ->() const { return _file_stream; }
  randomAccessFileStream* operator ()() const { return _file_stream; }

private:
  randomAccessFileStream*  _file_stream;
};

#define MAX_DEOPT_NUMBER 500

void JitProfileCacheInfo::init() {

  // param check
  check_param();
  if (_state == IS_ERR) {
    return;
  }
  
  _jit_profile_cache_dict = new JProfileCacheClassDictionary(PRELOAD_CLASS_HS_SIZE);
  // initialization parameters
  _method_loaded_count = 0;
  _state = IS_OK;

  if (ProfilingCacheFile == NULL) {
    _state = IS_ERR;
    return;
  }

  RandomFileStreamGuard fsg(new (ResourceObj::C_HEAP, mtInternal) randomAccessFileStream(
    ProfilingCacheFile, "rb+"));
  JitProfileCacheLogParser parser(fsg(), this);
  if (!fsg->is_open()) {
    jprofilecache_log_error(profilecache)("[JitProfileCache] ERROR : JitProfile doesn't exist");
    _state = IS_ERR;
    return;
  }
  parser.set_file_size(fsg->fileSize());

  // parse header
  if (!parser.parse_header()) {
    _state = IS_ERR;
    return;
  }

  // parse class
  if (!parser.parse_class()) {
    _state = IS_ERR;
    return;
  }

  // parse method
  while (parser.has_next_method_record()) {
    ProfileCacheMethodHold* holder = parser.parse_method();
    if (holder != NULL) {
      // count method parse successfully
      ++_method_loaded_count;
    }
    parser.increment_parsed_number_count();
  }
  jprofilecache_log_info(JitProfileCache)("JProfileCache [INFO]: parsed method number %d successful loaded %" PRIu64, parser.parsed_methods(), _method_loaded_count);
}

void JitProfileCacheInfo::check_param() {
  if (JProfilingCacheRecording) {
    jprofilecache_log_error(profilecache)("[JitProfileCache] ERROR: you can not set both JProfilingCacheCompileAdvance and JProfilingCacheRecording");
    _state = IS_ERR;
    return;
  }
  // check class data sharing
  if (UseSharedSpaces) {
    jprofilecache_log_error(profilecache)("[JitProfileCache] ERROR: when enable JProfilingCacheCompileAdvance, UseSharedSpaces must be disable");
    _state = IS_ERR;
    return;
  }

  // check CompilationProfileCacheDeoptNumOfMethodsPerIter
  if (CompilationProfileCacheDeoptNumOfMethodsPerIter == 0 || CompilationProfileCacheDeoptNumOfMethodsPerIter > MAX_DEOPT_NUMBER) {
    jprofilecache_log_error(profilecache)("[JitProfileCache] ERROR:CompilationProfileCacheDeoptNumOfMethodsPerIter is invalid must be large than 0 and less than or equal to 500.");
    _state = IS_ERR;
    return;
  }
  
  if (Arguments::mode() == Arguments::_int) {
    jprofilecache_log_error(profilecache)("[JitProfileCache] ERROR: when enable JProfilingCacheCompileAdvance, should not set -Xint");
    _state = IS_ERR;
    return;
  }
}



