/*
 * Copyright (c) 2025, Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2019 Alibaba Group Holding Limited. All Rights Reserved.
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

#ifndef SHARED_VM_JPROFILECACHE_JITPROFILECACHE_HPP
#define SHARED_VM_JPROFILECACHE_JITPROFILECACHE_HPP

#include "code/codeBlob.hpp"
#include "libadt/dict.hpp"
#include "memory/allocation.hpp"
#include "utilities/hashtable.hpp"
#include "utilities/linkedlist.hpp"
#include "utilities/ostream.hpp"
#include "utilities/globalDefinitions.hpp"
#include "utilities/growableArray.hpp"
#include "utilities/symbolRegexMatcher.hpp"
#include "runtime/timer.hpp"
#include "runtime/atomic.hpp"
#include "runtime/jniHandles.hpp"
#include "runtime/mutexLocker.hpp"
#include "oops/klass.hpp"
#include "oops/method.hpp"
#include "oops/methodData.hpp"
#include "oops/methodCounters.hpp"

class JitProfileRecorder;
class JitProfileCacheInfo;


#define INVALID_FIRST_INVOKE_INIT_ORDER -1

class JitProfileCache : public CHeapObj<mtInternal> {
public:
  enum JitProfileCacheState {
   NOT_INIT = 0,
   IS_OK = 1,
   IS_ERR = 2
  };
  
  unsigned int version()  { return _jit_profile_cache_version; }
  bool         is_valid() { return _jit_profile_cache_state == JitProfileCache::IS_OK; }
  
  void set_dummy_method(Method* m)          { _dummy_method = m; }
  Method* dummy_method()                    { return _dummy_method; }
  
  // init in JVM start
  void init();

  void set_log_level();

  static JitProfileCache* create_instance();
  static JitProfileCache* instance()  { return _jit_profile_cache_instance; }
  JitProfileRecorder* recorder()   { return _jit_profile_cache_recorder; }
  JitProfileCacheInfo*  preloader()  { return _jit_profile_cache_info; }
  // init JProfilingCacheRecording
  JitProfileCacheState init_for_recording();
  // init JProfilingCacheCompileAdvance
  JitProfileCacheState init_for_profilecache();
  
  SymbolRegexMatcher<mtClass>* excluding_matcher() { return _excluding_matcher; }
  
  JitProfileCacheState flush_recorder();
  
  static bool commit_compilation(methodHandle m, int bci, TRAPS);
  
  static Symbol* get_class_loader_name(ClassLoaderData* cld);


  bool profilecacheComplete;

protected:
  JitProfileCache();
  virtual ~JitProfileCache();

private:
  unsigned int                 _jit_profile_cache_version;
  static JitProfileCache*            _jit_profile_cache_instance;
  
  JitProfileCacheState               _jit_profile_cache_state;
  
  Method*                      _dummy_method;
  JitProfileRecorder*             _jit_profile_cache_recorder;
  JitProfileCacheInfo*              _jit_profile_cache_info;
  SymbolRegexMatcher<mtClass>*      _excluding_matcher;
};

// forward class
class JitProfileRecorder;
class ProfileCacheClassHolder;

class BytecodeProfileRecord : public CHeapObj<mtInternal> {
public:
  BytecodeProfileRecord() {  }
  ~BytecodeProfileRecord() {  }
};

class ProfileCacheMethodHold : public CHeapObj<mtInternal> {
  friend class ProfileCacheClassHolder;
public:
  ProfileCacheMethodHold(Symbol* name, Symbol* signature);
  ProfileCacheMethodHold(ProfileCacheMethodHold& rhs);
  virtual ~ProfileCacheMethodHold();

  Symbol* method_name()      const { return _method_name; }
  Symbol* method_signature() const { return _method_signature; }

  unsigned int invocation_count() const { return _invocation_count;}

  void set_interpreter_invocation_count(unsigned int value) { _interpreter_invocation_count = value; }
  void set_interpreter_exception_count(unsigned int value)   { _interpreter_exception_count = value; }
  void set_invocation_count(unsigned int value)      { _invocation_count = value; }
  void set_backage_count(unsigned int value)         { _backage_count = value; }

  void set_method_hash(unsigned int value)           { _method_hash = value; }
  void set_method_size(unsigned int value)           { _method_size = value; }
  void set_method_bci(int value)                     { _method_bci = value; }
  void set_mounted_offset(int value)          { _mounted_offset = value; }

  bool is_method_deopted()                 const { return _is_method_deopted; }
  void set_is_method_deopted(bool value)         { _is_method_deopted = value; }

  bool is_method_match(Method* method);

  ProfileCacheMethodHold* next() const                     { return _next; }
  void set_next(ProfileCacheMethodHold* h)                 { _next = h; }

  Method* resolved_method() const                       { return _resolved_method; }
  void set_resolved_method(Method* m)                   { _resolved_method = m; }

  GrowableArray<BytecodeProfileRecord*>* method_list()         const { return _method_list; }
  void set_method_list(GrowableArray<BytecodeProfileRecord*>* value) { _method_list = value; }

  ProfileCacheMethodHold* clone_and_add();

  bool is_alive(BoolObjectClosure* is_alive_closure) const;

private:
  Symbol*      _method_name;
  Symbol*      _method_signature;

  unsigned int _method_size;
  unsigned int _method_hash;
  int          _method_bci;

  unsigned int _interpreter_invocation_count;
  unsigned int _interpreter_exception_count;
  unsigned int _invocation_count;
  unsigned int _backage_count;

  int          _mounted_offset;

  bool         _owns_method_list;

  bool         _is_method_deopted;

  // A single LinkedList stores entries with the same initialization order
  ProfileCacheMethodHold*           _next;
  // The resolved method within the holder's list
  Method*                        _resolved_method;
  // An array of profile information, shared among entries with the same
  GrowableArray<BytecodeProfileRecord*>*  _method_list;
};

class ProfileCacheClassHolder : public CHeapObj<mtInternal> {
public:
  ProfileCacheClassHolder(Symbol* name, Symbol* loader_name,
                     Symbol* path, unsigned int size,
                     unsigned int hash, unsigned int crc32);
  virtual ~ProfileCacheClassHolder();

  void add_method(ProfileCacheMethodHold* mh) {
      assert(_class_method_list != NULL, "not initialize");
      _class_method_list->append(mh);
  }

  unsigned int             size() const              { return _class_size; }
  unsigned int             hash() const              { return _class_hash; }
  unsigned int             crc32() const             { return _class_crc32; }
  unsigned int             methods_count() const     { return _class_method_list->length(); }
  Symbol*                  class_name() const        { return _class_name; }
  Symbol*                  class_loader_name() const { return _class_loader_name; }
  Symbol*                  path() const              { return _class_path; }
  ProfileCacheClassHolder*      next() const              { return _next; }
  bool                     resolved() const          { return _class_resolved; }

  void                     set_resolved()            { _class_resolved = true; }
  void                     set_next(ProfileCacheClassHolder* h) { _next = h; }

  GrowableArray<ProfileCacheMethodHold*>* method_list() const { return _class_method_list; }

private:
  Symbol*                               _class_name;
  Symbol*                               _class_loader_name;
  Symbol*                               _class_path;

  unsigned int                          _class_size;
  unsigned int                          _class_hash;
  unsigned int                          _class_crc32;
  unsigned int                          _class_init_chain_index;

  bool                                  _class_resolved;

  GrowableArray<ProfileCacheMethodHold*>*  _class_method_list;
  
  ProfileCacheClassHolder*                   _next;
};

class ProfileCacheClassEntry : public HashtableEntry<Symbol*, mtInternal> {
  friend class JitProfileCacheInfo;
public:
  ProfileCacheClassEntry(ProfileCacheClassHolder* holder)
      : _head_holder(holder),
        _chain_offset(-1),
        _class_loader_name(NULL),
        _class_path(NULL) {
  }

  ProfileCacheClassEntry()
      : _head_holder(NULL),
        _chain_offset(-1),
        _class_loader_name(NULL),
        _class_path(NULL) {
  }

  virtual ~ProfileCacheClassEntry() {  }

  void init() {
      _head_holder = NULL;
      _chain_offset = -1;
      _class_loader_name = NULL;
      _class_path = NULL;
  }

  ProfileCacheClassHolder* head_holder()                          { return _head_holder; }
  void                set_head_holder(ProfileCacheClassHolder* h) { _head_holder = h; }

  int                 chain_offset()               { return _chain_offset; }
  void                set_chain_offset(int offset) { _chain_offset = offset; }

  Symbol*             class_loader_name()                { return _class_loader_name; }
  void                set_class_loader_name(Symbol* s)   { _class_loader_name = s; }
  Symbol*             class_path()                       { return _class_path; }
  void                set_class_path(Symbol* s)          { _class_path = s; }


  ProfileCacheClassEntry*  next() {
      return (ProfileCacheClassEntry*)HashtableEntry<Symbol*, mtInternal>::next();
  }

  void add_class_holder(ProfileCacheClassHolder* h) {
      h->set_next(_head_holder);
      _head_holder = h;
  }

  ProfileCacheClassHolder* find_class_holder(unsigned int size, unsigned int crc32);

private:
  int                 _chain_offset;
  Symbol*             _class_loader_name;
  Symbol*             _class_path;
  ProfileCacheClassHolder* _head_holder;

};

class JProfileCacheClassDictionary : public Hashtable<Symbol*, mtInternal> {
public:
  JProfileCacheClassDictionary(int size);
  virtual ~JProfileCacheClassDictionary();

  ProfileCacheClassEntry* find_entry(unsigned int hash_value, Symbol* name,
                                Symbol* loader_name, Symbol* path);

  ProfileCacheClassEntry* find_head_entry(unsigned int hash_value, Symbol* name);

  ProfileCacheClassEntry* find_entry(InstanceKlass* k);

  ProfileCacheClassEntry* bucket(int i) {
      return (ProfileCacheClassEntry*)Hashtable<Symbol*, mtInternal>::bucket(i);
  }

  ProfileCacheClassEntry* find_or_create_class_entry(unsigned int hash_value, Symbol* symbol,
                                              Symbol* loader_name, Symbol* path,
                                              int order);

private:

  ProfileCacheClassEntry* new_entry(Symbol* symbol);
};

class ProfileCacheClassChain;

class MethodHolderIterator {
public:
  MethodHolderIterator()
    : _profile_cache_class_chain(NULL),
      _current_method_hold(NULL),
      _holder_index(-1) {
  }

  MethodHolderIterator(ProfileCacheClassChain* chain, ProfileCacheMethodHold* holder, int index)
    : _profile_cache_class_chain(chain),
      _current_method_hold(holder),
      _holder_index(index) {
  }

  ~MethodHolderIterator() { }

  ProfileCacheMethodHold* operator*() { return _current_method_hold; }

  int index() { return _holder_index; }

  bool initialized() { return _profile_cache_class_chain != NULL; }

  ProfileCacheMethodHold* next();

private:
  int                  _holder_index;  // current holder's position in ProfileCacheClassChain
  ProfileCacheClassChain*      _profile_cache_class_chain;
  ProfileCacheMethodHold* _current_method_hold;

};

class ProfileCacheClassChain : public CHeapObj<mtClass> {
public:
  class ProfileCacheClassChainEntry : public CHeapObj<mtClass> {
  public:
    enum ClassState {
      _not_loaded = 0,
      _load_skipped,
      _class_loaded,
      _class_inited
    };

    ProfileCacheClassChainEntry()
      : _class_name(NULL),
        _class_loader_name(NULL),
        _class_path(NULL),
        _class_state(_not_loaded),
        _method_holder(NULL),
        _resolved_klasses(new (ResourceObj::C_HEAP, mtClass)
                          GrowableArray<InstanceKlass*>(1, true, mtClass)),
        _method_keep_holders(new (ResourceObj::C_HEAP, mtClass)
                          GrowableArray<jobject>(1, true)) {  }

    ProfileCacheClassChainEntry(Symbol* class_name, Symbol* loader_name, Symbol* path)
      : _class_name(class_name),
        _class_loader_name(loader_name),
        _class_path(path),
        _class_state(_not_loaded),
        _method_holder(NULL),
        _resolved_klasses(new (ResourceObj::C_HEAP, mtClass)
                          GrowableArray<InstanceKlass*>(1, true, mtClass)),
        _method_keep_holders(new (ResourceObj::C_HEAP, mtClass)
                          GrowableArray<jobject>(1, true)) {  }

    virtual ~ProfileCacheClassChainEntry() { 
      if(!_method_keep_holders->is_empty()) {
        int len = _method_keep_holders->length();
        for (int i = 0; i < len; i++) {
          JNIHandles::destroy_global(_method_keep_holders->at(i));
        }
      }
    }

    Symbol*        class_name()                  const { return _class_name; }
    Symbol*        class_loader_name()                 const { return _class_loader_name; }
    Symbol*        class_path()                        const { return _class_path; }
    void           set_class_name(Symbol* name)  { _class_name = name; }
    void           set_class_loader_name(Symbol* name) { _class_loader_name = name; }
    void           set_class_path(Symbol* path)        { _class_path = path; }

    GrowableArray<InstanceKlass*>* resolved_klasses()
                   { return _resolved_klasses; }

    GrowableArray<jobject>* method_keep_holders()
                   { return _method_keep_holders; }

    // entry state
    bool           is_not_loaded()        const { return _class_state == _not_loaded; }
    bool           is_skipped()           const { return _class_state == _load_skipped; }
    bool           is_loaded()            const { return _class_state == _class_loaded; }
    bool           is_inited()            const { return _class_state == _class_inited; }
    void           set_not_loaded()       { _class_state = _not_loaded; }
    void           set_skipped()          { _class_state = _load_skipped; }
    void           set_loaded()           { _class_state = _class_loaded; }
    void           set_inited()           { _class_state = _class_inited; }

    void           set_class_state(int state) { _class_state = state;}

    int            class_state()                { return _class_state; }

    void add_method_holder(ProfileCacheMethodHold* h) {
      h->set_next(_method_holder);
      _method_holder = h;
    }

    bool is_all_initialized();

    bool contains_redefined_class();

    InstanceKlass* get_first_uninitialized_klass();

    ProfileCacheMethodHold* method_holder()  { return _method_holder; }

  private:
    int                                  _class_state;

    Symbol*                              _class_name;
    Symbol*                              _class_loader_name;
    Symbol*                              _class_path;

    ProfileCacheMethodHold*              _method_holder;
    GrowableArray<InstanceKlass*>*       _resolved_klasses;
    GrowableArray<jobject>*              _method_keep_holders;
  };

  ProfileCacheClassChain(unsigned int size);
  virtual ~ProfileCacheClassChain();

  enum ClassChainState {
    NOT_INITED = 0,
    INITED = 1,
    PRE_PROFILECACHE = 2,
    PROFILECACHE_COMPILING = 3,
    PROFILECACHE_DONE = 4,
    PROFILECACHE_PRE_DEOPTIMIZE = 5,
    PROFILECACHE_DEOPTIMIZING = 6,
    PROFILECACHE_DEOPTIMIZED = 7,
    PROFILECACHE_ERROR_STATE = 8
  };
  const char* get_state(ClassChainState state);
  bool try_transition_to_state(ClassChainState new_state);
  ClassChainState current_state() { return _state; }

  int  class_chain_inited_index()        const { return _class_chain_inited_index; }
  int  loaded_index()        const { return _loaded_class_index; }
  int  length()              const { return _length; }

  void set_loaded_index(int index)         { _loaded_class_index = index; }
  void set_length(int length)              { _length = length; }
  void set_inited_index(int index)         { _class_chain_inited_index = index; }

  JitProfileCacheInfo* holder() { return _holder; }
  void set_holder(JitProfileCacheInfo* preloader) { _holder = preloader; }

  bool notify_deopt_signal() {
    return try_transition_to_state(PROFILECACHE_PRE_DEOPTIMIZE);
  }

  bool can_record_class() {
    return _state == INITED || _state == PRE_PROFILECACHE || _state == PROFILECACHE_COMPILING;
  }

  bool deopt_has_done() {
    return _state == PROFILECACHE_DEOPTIMIZED;
  }

  void mark_loaded_class(InstanceKlass* klass);

  ProfileCacheClassChainEntry* at(int index) { return &_entries[index]; }

  void refresh_indexes();

  void precompilation();


  // count method
  void add_method_at_index(ProfileCacheMethodHold* mh, int index);

  bool compile_method(ProfileCacheMethodHold* mh);

  void unload_class(BoolObjectClosure* is_alive);

  void deopt_prologue();
  void deopt_epilogue();

  bool should_deoptimize_methods();

  // deoptimize number methods per invocation
  void deoptimize_methods();

  void invoke_deoptimize_vmop();

  void preload_class_in_constantpool();

private:
  int                   _class_chain_inited_index;
  int                   _loaded_class_index;
  int                   _length;

  volatile ClassChainState _state;

  ProfileCacheClassChainEntry*  _entries;

  JitProfileCacheInfo*       _holder;

  TimeStamp             _init_timestamp;
  TimeStamp             _last_timestamp;

  int                   _deopt_index;
  ProfileCacheMethodHold*  _deopt_cur_holder;

  bool                  _has_unmarked_compiling_flag;

  void handle_duplicate_class(InstanceKlass* k, int chain_index);

  void resolve_class_methods(InstanceKlass* k, ProfileCacheClassHolder* holder, int chain_index);

  void update_class_chain(InstanceKlass* ky, int chain_index);

  void compile_methodholders_queue(Stack<ProfileCacheMethodHold*, mtInternal>& compile_queue);

  void update_loaded_index(int index);

  ProfileCacheMethodHold* resolve_method_info(Method* method,
                                           ProfileCacheClassHolder* holder);
};

class JitProfileCacheInfo : public CHeapObj<mtInternal> {
public:
  enum JitProfileCacheInfoState {
    NOT_INIT = 0,
    IS_OK = 1,
    IS_ERR = 2
  };

  JitProfileCacheInfo();
  virtual ~JitProfileCacheInfo();

  bool is_valid() { return _state == IS_OK; }
  void init();
  void check_param();

  bool should_preload_class(Symbol* s);

  JProfileCacheClassDictionary* jit_profile_cache_dict() { return _jit_profile_cache_dict; }
  uint64_t                loaded_count() { return _method_loaded_count; }

  ProfileCacheClassChain*      chain() { return _profile_cache_chain; }
  void                    set_chain(ProfileCacheClassChain* chain) { _profile_cache_chain = chain; }

  JitProfileCache*              holder() { return _holder; }
  void                    set_holder(JitProfileCache* h) { _holder = h; }

  bool resolve_loaded_klass(InstanceKlass* klass);

  void jvm_booted_is_done();

  void notify_precompilation();

  static Symbol* remove_meaningless_suffix(Symbol* s);

private:
  JitProfileCacheInfoState         _state;
  JitProfileCache*                 _holder;
  JProfileCacheClassDictionary*    _jit_profile_cache_dict;
  ProfileCacheClassChain*          _profile_cache_chain;
  uint64_t                         _method_loaded_count;
  bool                             _jvm_booted_is_done;
};

#endif //SHARED_VM_JPROFILECACHE_JITPROFILECACHE_HPP
