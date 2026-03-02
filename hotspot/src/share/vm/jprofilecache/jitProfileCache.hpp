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
 */

#ifndef SHARED_VM_JPROFILECACHE_JITPROFILECACHE_HPP
#define SHARED_VM_JPROFILECACHE_JITPROFILECACHE_HPP

#include "code/codeBlob.hpp"
#include "libadt/dict.hpp"
#include "memory/allocation.hpp"
#include "utilities/hashtable.hpp"
#include "jprofilecache/jitProfileClassChain.hpp"
#include "jprofilecache/jitProfileCacheClass.hpp"
#include "utilities/symbolRegexMatcher.hpp"
#include "utilities/linkedlist.hpp"
#include "utilities/ostream.hpp"
#include "utilities/globalDefinitions.hpp"
#include "utilities/growableArray.hpp"
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

// Manager of the feature, created when vm is started
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

  // static Symbol* get_class_loader_name(ClassLoaderData* cls);

  bool profilecacheComplete;

protected:
  JitProfileCache();
  virtual ~JitProfileCache();

private:
  JitProfileCacheState               _jit_profile_cache_state;
  unsigned int                       _jit_profile_cache_version;
  static JitProfileCache*            _jit_profile_cache_instance;
  Method*                            _dummy_method;
  JitProfileRecorder*                _jit_profile_cache_recorder;
  JitProfileCacheInfo*               _jit_profile_cache_info;
  SymbolRegexMatcher<mtClass>*       _excluding_matcher;
};

// forward class
class JitProfileRecorder;

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

  private:
  JProfileCacheClassDictionary*    _jit_profile_cache_dict;
  ProfileCacheClassChain*          _profile_cache_chain;
  uint64_t                         _method_loaded_count;
  JitProfileCacheInfoState         _state;
  JitProfileCache*                 _holder;
  bool                             _jvm_booted_is_done;
};

#endif //SHARED_VM_JPROFILECACHE_JITPROFILECACHE_HPP
