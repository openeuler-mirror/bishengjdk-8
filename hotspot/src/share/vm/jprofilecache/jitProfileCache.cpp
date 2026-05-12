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

#include "precompiled.hpp"
#include "classfile/classLoaderData.hpp"
#include "classfile/symbolTable.hpp"
#include "classfile/systemDictionary.hpp"
#include "compiler/compileBroker.hpp"
#include "utilities/hashtable.inline.hpp"
#include "jprofilecache/jitProfileCache.hpp"
#include "jprofilecache/jitProfileRecord.hpp"
#include "jprofilecache/jitProfileCacheFileParser.hpp"
#include "jprofilecache/jitProfileCacheUtils.hpp"
#include "jprofilecache/jitProfileCacheLog.hpp"
#include "oops/method.hpp"
#include "oops/typeArrayKlass.hpp"
#include "runtime/arguments.hpp"
#include "runtime/compilationPolicy.hpp"
#include "runtime/handles.inline.hpp"
#include "runtime/javaCalls.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/os.hpp"
#include "runtime/thread.hpp"
#include "utilities/defaultStream.hpp"
#include "utilities/stack.hpp"
#include "utilities/stack.inline.hpp"
#include "runtime/atomic.hpp"
#include "libadt/dict.hpp"

JitProfileCache*                JitProfileCache::_jit_profile_cache_instance         = NULL;
bool                            JitProfileCache::_log_level_initialized              = false;
bool                            JitProfileCache::_log_level_valid                    = true;

static inline char jprofilecache_to_lower(char c) {
  if (c >= 'A' && c <= 'Z') {
    return (char)(c - 'A' + 'a');
  }
  return c;
}

static bool jprofilecache_equals_ignore_case(const char* a, const char* b) {
  if (a == NULL || b == NULL) {
    return false;
  }
  while (*a != '\0' && *b != '\0') {
    if (jprofilecache_to_lower(*a) != jprofilecache_to_lower(*b)) {
      return false;
    }
    a++;
    b++;
  }
  return (*a == '\0' && *b == '\0');
}

JitProfileCache::JitProfileCache()
  : profilecacheComplete(false),
    _jit_profile_cache_state(NOT_INIT),
    _jit_profile_cache_version(JITPROFILECACHE_VERSION),
    _dummy_method(NULL),
    _jit_profile_cache_recorder(NULL),
    _jit_profile_cache_info(NULL),
    _excluding_matcher(NULL) {}

JitProfileCache::~JitProfileCache() {
  delete _jit_profile_cache_recorder;
  delete _jit_profile_cache_info;
}

JitProfileCache* JitProfileCache::create_instance() {
  _jit_profile_cache_instance = new JitProfileCache();
  return _jit_profile_cache_instance;
}

bool JitProfileCache::set_log_level() {
  if (_log_level_initialized) {
    return _log_level_valid;
  }
  _log_level_initialized = true;

  if (ProfilingCacheLogLevel == NULL) {
    _log_level_valid = false;
    return false;
  }

  if (jprofilecache_equals_ignore_case(ProfilingCacheLogLevel, "trace")) {
    LogLevel::LogLevelNum = LogLevel::Trace;
  } else if (jprofilecache_equals_ignore_case(ProfilingCacheLogLevel, "debug")) {
    LogLevel::LogLevelNum = LogLevel::Debug;
  } else if (jprofilecache_equals_ignore_case(ProfilingCacheLogLevel, "info")) {
    LogLevel::LogLevelNum = LogLevel::Info;
  } else if (jprofilecache_equals_ignore_case(ProfilingCacheLogLevel, "warning")) {
    LogLevel::LogLevelNum = LogLevel::Warning;
  } else if (jprofilecache_equals_ignore_case(ProfilingCacheLogLevel, "error")) {
    LogLevel::LogLevelNum = LogLevel::Error;
  } else if (jprofilecache_equals_ignore_case(ProfilingCacheLogLevel, "off")) {
    LogLevel::LogLevelNum = LogLevel::Off;
  } else {
    _log_level_valid = false;
    return false;
  }
  _log_level_valid = true;
  return true;
}

JitProfileCache::JitProfileCacheState JitProfileCache::init_for_recording() {
  assert(JProfilingCacheRecording && !JProfilingCacheCompileAdvance, " JitProfileCache JVM option verify failure");
  _jit_profile_cache_recorder = new JitProfileRecorder();
  _jit_profile_cache_recorder->init();

  // check state
  if (_jit_profile_cache_recorder->is_valid()) {
    _jit_profile_cache_state = JitProfileCache::IS_OK;
  } else  {
    _jit_profile_cache_state = JitProfileCache::IS_ERR;
  }
  return _jit_profile_cache_state;
}

JitProfileCache::JitProfileCacheState JitProfileCache::init_for_profilecache() {
  assert(!JProfilingCacheRecording && JProfilingCacheCompileAdvance, "JitProfileCache JVM option verify failure");
  if (CompilationProfileCacheExclude != NULL) {
    _excluding_matcher = new (ResourceObj::C_HEAP, mtClass) SymbolRegexMatcher<mtClass>(CompilationProfileCacheExclude);
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

  // set log level
  if (!set_log_level()) {
    jio_fprintf(defaultStream::error_stream(),
                "Improperly specified VM option 'ProfilingCacheLogLevel=%s'\n",
                ProfilingCacheLogLevel);
    vm_exit(-1);
    return;
  }

  if (TieredStopAtLevel < CompLevel_none || TieredStopAtLevel > CompLevel_full_optimization) {
    jprofilecache_log_error(jprofilecache,
                            "intx TieredStopAtLevel=" INTX_FORMAT " is outside the allowed range [ %d ... %d ]",
                            TieredStopAtLevel,
                            CompLevel_none,
                            CompLevel_full_optimization);
    jprofilecache_log_error(jprofilecache,
                            "Improperly specified VM option 'TieredStopAtLevel=" INTX_FORMAT "'",
                            TieredStopAtLevel);
    _jit_profile_cache_state = IS_ERR;
    vm_exit(-1);
    return;
  }

  if (_jit_profile_cache_state != IS_ERR && JProfilingCacheCompileAdvance) {
    init_for_profilecache();
  } else if (_jit_profile_cache_state != IS_ERR && JProfilingCacheRecording) {
    init_for_recording();
  }
  if ((JProfilingCacheRecording || JProfilingCacheCompileAdvance) && !JitProfileCache::is_valid()) {
    jprofilecache_log_error(jprofilecache, "JProfileCache init error.");
    vm_exit(-1);
  }
}

JitProfileCache::JitProfileCacheState JitProfileCache::flush_recorder() {
  if(_jit_profile_cache_state == IS_ERR) {
    return _jit_profile_cache_state;
  }
  if (!_jit_profile_cache_recorder->flush_record()) {
    return IS_ERR;
  }
  if (!_jit_profile_cache_recorder->is_valid()) {
    _jit_profile_cache_state = IS_ERR;
    return _jit_profile_cache_state;
  }
  _jit_profile_cache_state = IS_OK;
  return _jit_profile_cache_state;
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

void JitProfileCacheInfo::jvm_booted_is_done() {
  _jvm_booted_is_done = true;
  ProfileCacheClassChain* chain = this->chain();
  assert(chain != NULL, "ProfileCacheClassChain is NULL");
}

void JitProfileCacheInfo::notify_precompilation() {
  ProfileCacheClassChain *chain = this->chain();
  assert(chain != NULL, "ProfileCacheClassChain is NULL");
  chain->try_transition_to_state(ProfileCacheClassChain::PRE_PROFILECACHE);

  // preload class
  jprofilecache_log_info(jprofilecache, "start preload class from constant pool");
  chain->preload_class_in_constantpool();

  // precompile cache method
  jprofilecache_log_info(jprofilecache, "start profilecache compilation");
  chain->precompilation();
  Thread *THREAD = Thread::current();
  if (HAS_PENDING_EXCEPTION) {
    return;
  }

  if (!chain->try_transition_to_state(ProfileCacheClassChain::PROFILECACHE_DONE)) {
    jprofilecache_log_error(jprofilecache, "can not change state to PROFILECACHE_DONE");
  } else {
    jprofilecache_log_info(jprofilecache, "profilecache compilation is done");
  }
}

bool JitProfileCacheInfo::should_preload_class(Symbol* s) {
  if (UseJProfilingCacheSystemBlackList &&
      JitProfileCacheUtils::is_in_unpreloadable_classes_black_list(s)) {
    return false;
  }
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
    MutexLocker mu(ProfileCacheClassChain_lock);
    if (!chain()->can_record_class()) {
      return false;
    }
  }
  k->set_jprofilecache_recorded(true);
  chain()->mark_loaded_class(k, jit_profile_cache_dict()->find_entry(k));
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
  if (JProfilingCacheRecording) {
    jprofilecache_log_error(jprofilecache, "you can not set both JProfilingCacheCompileAdvance and JProfilingCacheRecording");
    _state = IS_ERR;
    return;
  }

  _jit_profile_cache_dict = new JProfileCacheClassDictionary(PRELOAD_CLASS_HS_SIZE);
  // initialization parameters
  _method_loaded_count = 0;
  _state = IS_OK;

  if (JProfilingCacheAutoArchiveDir != NULL) {
    ProfilingCacheFile = JitProfileRecorder::auto_jpcfile_name();
  }

  if (ProfilingCacheFile == NULL) {
    _state = IS_ERR;
    return;
  }

  RandomFileStreamGuard fsg(new (ResourceObj::C_HEAP, mtInternal) randomAccessFileStream(
    ProfilingCacheFile, "rb"));
  JitProfileCacheFileParser parser(fsg(), this);
  if (!fsg->is_open()) {
    jprofilecache_log_error(jprofilecache, "JitProfile doesn't exist");
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
  jprofilecache_log_info(jprofilecache, "parsed method number %d successful loaded %" PRIu64, parser.parsed_methods(), _method_loaded_count);
}
