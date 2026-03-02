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
#include "runtime/arguments.hpp"
#include "runtime/javaCalls.hpp"
#include "runtime/thread.hpp"
#include "runtime/atomic.hpp"
#include "memory/allocation.hpp"
#include "classfile/symbolTable.hpp"
#include "classfile/systemDictionary.hpp"
#include "compiler/compileBroker.hpp"
#include "jprofilecache/jitProfileCache.hpp"
#include "jprofilecache/jitProfileCacheClass.hpp"
#include "jprofilecache/jitProfileCacheFileParser.hpp"
#include "jprofilecache/jitProfileCacheUtils.hpp"
#include "jprofilecache/jitProfileClassChain.hpp"
#include "jprofilecache/jitProfileRecord.hpp"
#include "libadt/dict.hpp"
#include "jprofilecache/jitProfileCacheLog.hpp"

// offset
#define PROFILECACHE_VERSION_OFFSET                  0
#define PROFILECACHE_MAGIC_NUMBER_OFFSET             4
#define FILE_SIZE_OFFSET                             8
#define PROFILECACHE_CRC32_OFFSET                    12
#define APPID_OFFSET                                 16
#define MAX_SYMBOL_LENGTH_OFFSET                     20
#define RECORD_COUNT_OFFSET                          24
#define PROFILECACHE_TIME_OFFSET                     28

#define HEADER_SIZE                                  36

// width section
#define RECORE_VERSION_WIDTH (PROFILECACHE_MAGIC_NUMBER_OFFSET - PROFILECACHE_VERSION_OFFSET)
#define RECORE_MAGIC_WIDTH (FILE_SIZE_OFFSET - PROFILECACHE_MAGIC_NUMBER_OFFSET)
#define FILE_SIZE_WIDTH (PROFILECACHE_CRC32_OFFSET - FILE_SIZE_OFFSET)
#define RECORE_CRC32_WIDTH (APPID_OFFSET - PROFILECACHE_CRC32_OFFSET)
#define RECORE_APPID_WIDTH (MAX_SYMBOL_LENGTH_OFFSET - APPID_OFFSET)
#define RECORE_MAX_SYMBOL_LENGTH_WIDTH (RECORD_COUNT_OFFSET - MAX_SYMBOL_LENGTH_OFFSET)
#define RECORD_COUNTS_WIDTH (PROFILECACHE_TIME_OFFSET - RECORD_COUNT_OFFSET)
#define RECORE_TIME_WIDTH (HEADER_SIZE - PROFILECACHE_TIME_OFFSET)

// value
#define RECORE_FILE_DEFAULT_NUMBER             0
#define RECORE_CRC32_DEFAULT_NUMBER            0

#define ARENA_SIZE       128
#define READ_U1_INTERVAL 1
#define READ_U4_INTERVAL 4
#define READ_U8_INTERVAL 8

#define JVM_DEFINE_CLASS_PATH "_JVM_DefineClass_"

JitProfileCacheFileParser::JitProfileCacheFileParser(randomAccessFileStream* fs, JitProfileCacheInfo* holder)
        : _is_valid(false),
          _has_parsed_header(false),
          _file_size(0),
          _position(0),
          _parsed_method_count(0),
          _total_recorder_method(0),
          _parsed_version(0),
          _file_stream(fs),
          _max_symbol_length(0),
          _parse_str_buf(NULL),
          _holder(holder),
          _arena(new (mtInternal) Arena(mtInternal, ARENA_SIZE)) {
}

JitProfileCacheFileParser::~JitProfileCacheFileParser() {
  delete _arena;
}

char parse_int_buf[8];
u1 JitProfileCacheFileParser::read_u1() {
  _file_stream->read(parse_int_buf, 1, 1);
  _position += READ_U1_INTERVAL;
  return *(u1*)parse_int_buf;
}

u4 JitProfileCacheFileParser::read_u4() {
  _file_stream->read(parse_int_buf, READ_U4_INTERVAL, 1);
  _position += READ_U4_INTERVAL;
  return *(u4*)parse_int_buf;
}

u8 JitProfileCacheFileParser::read_u8() {
  _file_stream->read(parse_int_buf, READ_U8_INTERVAL, 1);
  _position += READ_U8_INTERVAL;
  return *(u8*)parse_int_buf;
}

const char* JitProfileCacheFileParser::read_string() {
  int current_read_pos = 0;
  do {
    _file_stream->read(_parse_str_buf + current_read_pos, 1, 1);
    current_read_pos++;
  } while (*(_parse_str_buf + current_read_pos - 1) != '\0'
           && current_read_pos <= _max_symbol_length + 1);

  _position += current_read_pos;
  int actual_string_length = current_read_pos - 1;
  if (actual_string_length == 0) {
    jprofilecache_log_warning(jprofilecache, "[JitProfileCache] WARNING : Parsed empty symbol at position %d\n", _position);
    return "";
  } else if (actual_string_length > max_symbol_length()) {
    jprofilecache_log_error(jprofilecache, "[JitProfileCache] ERROR : The parsed symbol length exceeds %d\n", max_symbol_length());
    return NULL;
  } else {
    char* parsed_string = NEW_RESOURCE_ARRAY(char, actual_string_length + 1);
    memcpy(parsed_string, _parse_str_buf, actual_string_length + 1);
    return parsed_string;
  }
}

BytecodeProfileRecord* JitProfileCacheFileParser::read_data_layout() {
  u4 size = read_u4();
  BytecodeProfileRecord* bpr = new BytecodeProfileRecord(size);
  _file_stream->read(bpr->data(), size, 1);
  _position += size;
  return bpr;
}

#define MAX_COUNT_VALUE (1024 * 1024 * 128)

bool JitProfileCacheFileParser::logparse_illegal_check(const char* s, bool ret_value, int end_position) {
  if (_position > end_position) {
    jprofilecache_log_error(jprofilecache, "[JitProfileCache] ERROR : read out of bound, "
                      "file format error");
    return ret_value;
  }
  if (s == NULL) {
    _position = end_position;
    jprofilecache_log_error(jprofilecache, "[JitProfileCache] ERROR : illegal string in log file");
    return ret_value;
  }
  return true;
}

bool JitProfileCacheFileParser::logparse_illegal_count_check(int cnt, bool ret_value, int end_position) {
  if (_position > end_position) {
    jprofilecache_log_error(jprofilecache, "[JitProfileCache] ERROR : read out of bound, "
                      "file format error");
    return ret_value;
  }
  if ((u4)cnt > MAX_COUNT_VALUE) {
    _position = end_position;
    jprofilecache_log_error(jprofilecache, "[JitProfileCache] ERROR : illegal count ("
                      UINT32_FORMAT ") too big", cnt);
    return ret_value;
  }
  return true;
}

bool JitProfileCacheFileParser::should_ignore_this_class(Symbol* symbol) {
  // deal with spring auto-generated
  ResourceMark rm;
  char* name = symbol->as_C_string();
  static const char* const CGLIB_SIG = "CGLIB$$";
  static const char* const ACCESSER_SUFFIX = "ConstructorAccess";
  if (::strstr(name, CGLIB_SIG) != NULL ||
      ::strstr(name, ACCESSER_SUFFIX) != NULL ||
      (UseJProfilingCacheSystemBlackList &&
       JitProfileCacheUtils::is_in_unpreloadable_classes_black_list(name))) {
    jprofilecache_log_debug(jprofilecache, "[JitProfileCache] DEBUG : Ignore class %s", name);
    return true;
  }
  JitProfileCache* jprofilecache = info_holder()->holder();
  SymbolRegexMatcher<mtClass>* matcher = jprofilecache->excluding_matcher();
  if (matcher == NULL) {
    return false;
  }
  if (matcher->matches(symbol)) {
    jprofilecache_log_debug(jprofilecache, "[JitProfileCache] DEBUG : Ignore class %s", name);
    return true;
  } else {
    return false;
  }
}

#define SYMBOL_TERMINATOR_SPACE 2

bool JitProfileCacheFileParser::parse_header() {
  int begin_position = _position;
  int end_position = begin_position + HEADER_SIZE;
  u4 parse_version = read_u4();
  u4 parse_magic_number = read_u4();
  u4 parse_file_size = read_u4();
  int parse_crc32_recorded = (int)read_u4();
  u4 appid = read_u4();
  unsigned int version = JitProfileCache::instance()->version();

  if (parse_version < JITPROFILECACHE_VERSION_V1 || parse_version > version) {
    _is_valid = false;
    jprofilecache_log_error(jprofilecache,
                            "[JitProfileCache] ERROR : unsupported profile version %d, supported range is [%d, %d]",
                            parse_version, JITPROFILECACHE_VERSION_V1, version);
    return false;
  }
  _parsed_version = parse_version;
  if (parse_magic_number != JPROFILECACHE_MAGIC_NUMBER
      || (long)parse_file_size != this->file_size()) {
    _is_valid = false;
    jprofilecache_log_error(jprofilecache, "[JitProfileCache] ERROR : illegal header");
    return false;
  }
  // valid appid
  if (CompilationProfileCacheAppID != 0 && CompilationProfileCacheAppID != appid) {
    _is_valid = false;
    jprofilecache_log_error(jprofilecache, "[JitProfileCache] ERROR : illegal CompilationProfileCacheAppID");
    return false;
  }
  // valid crc32
  int crc32_actual = JitProfileRecorder::compute_crc32(_file_stream);
  if (parse_crc32_recorded != crc32_actual) {
    _is_valid = false;
    jprofilecache_log_error(jprofilecache, "[JitProfileCache] ERROR : JitProfile crc32 check failure");
    return false;
  }

  u4 parse_max_symbol_length = read_u4();
  logparse_illegal_count_check(parse_max_symbol_length, false, end_position);
  _parse_str_buf = (char*)_arena->Amalloc(parse_max_symbol_length + SYMBOL_TERMINATOR_SPACE);
  _max_symbol_length = (int)parse_max_symbol_length;

  u4 parse_record_count = read_u4();
  logparse_illegal_count_check(parse_record_count, false, end_position);
  _total_recorder_method = parse_record_count;
  u4 utc_time = read_u8();
  _is_valid = true;
  return true;
}

Symbol* JitProfileCacheFileParser::create_symbol(const char* char_name) {
  Thread* THREAD = Thread::current();
  return SymbolTable::new_symbol(char_name, strlen(char_name), THREAD);
}

bool JitProfileCacheFileParser::parse_class() {
  ResourceMark rm;
  int begin_position = _position;
  u4 section_size = read_u4();
  int end_position = begin_position + (int)section_size;
  u4 parse_cnt = read_u4();
  logparse_illegal_count_check(parse_cnt, false, end_position);
  const bool has_clinit_status = parsed_version() >= JITPROFILECACHE_VERSION_V2;

  ProfileCacheClassChain* chain = new ProfileCacheClassChain(parse_cnt);
  info_holder()->set_chain(chain);

  for (int i = 0; i < (int)parse_cnt; i++) {
    const char* parse_name_char = read_string();
    logparse_illegal_check(parse_name_char, false, end_position);
    const char* parse_loader_char = read_string();
    logparse_illegal_check(parse_loader_char, false, end_position);
    const char* parse_path_char = read_string();
    logparse_illegal_check(parse_path_char, false, end_position);
    bool clinit_succeeded = true;
    if (has_clinit_status) {
      u1 parse_clinit_status = read_u1();
      logparse_illegal_count_check(parse_clinit_status, false, end_position);
      if (parse_clinit_status != 0 && parse_clinit_status != 1) {
        jprofilecache_log_error(jprofilecache,
                                "[JitProfileCache] ERROR : illegal class init status %u",
                                parse_clinit_status);
        _position = end_position;
        return false;
      }
      clinit_succeeded = parse_clinit_status == 1;
    }
    Symbol* name = create_symbol(parse_name_char);
    Symbol* loader_name = create_symbol(parse_loader_char);
    Symbol* path = create_symbol(parse_path_char);
    loader_name = JitProfileCacheUtils::remove_meaningless_suffix(loader_name);
    chain->at(i)->set_class_name(name);
    chain->at(i)->set_class_loader_name(loader_name);
    chain->at(i)->set_class_path(path);
    chain->at(i)->set_recorded_clinit_succeeded(clinit_succeeded);

    check_class(i, name, loader_name, path, chain);

  } // end of for loop

  // check section size
  if (_position - begin_position != (int)section_size) {
    jprofilecache_log_error(jprofilecache, "[JitProfileCache] ERROR : JitProfile class parse fail");
    return false;
  }
  return true;
}

void JitProfileCacheFileParser::check_class(int i, Symbol* name, Symbol* loader_name, Symbol* path, ProfileCacheClassChain* chain) {
  // add to preload class dictionary
  unsigned int hash_value = name->identity_hash();
  ProfileCacheClassEntry* e = info_holder()->jit_profile_cache_dict()->
          find_or_create_class_entry(hash_value, name, loader_name, path, i);
  // e->chain_offset() < i : means same class symbol already existed in the chain
  // should_ignore_this_class(name): means this class is in skipped list(build-in or user-defined)
  // so set entry state is skipped, will be ignored in JitProfileCache
  if (e->chain_offset() < i || should_ignore_this_class(name)) {
    chain->at(i)->set_skipped();
  } else {
    Symbol* name_no_suffix = JitProfileCacheUtils::remove_meaningless_suffix(name);
    if (name_no_suffix->fast_compare(name) != 0) {
      unsigned int hash_no_suffix = name_no_suffix->identity_hash();
      ProfileCacheClassEntry* e_no_suffix = info_holder()->jit_profile_cache_dict()->
              find_or_create_class_entry(hash_no_suffix, name_no_suffix, loader_name, path, i);
      if (e_no_suffix->chain_offset() < i) {
        chain->at(i)->set_skipped();
      }
    }
  }
}

bool JitProfileCacheFileParser::valid() {
  if(!_has_parsed_header) {
    parse_header();
  }
  return _is_valid;
}

bool JitProfileCacheFileParser::has_next_method_record() {
  return _parsed_method_count < _total_recorder_method && _position < _file_size;
}

ProfileCacheMethodHold* JitProfileCacheFileParser::parse_method() {
  ResourceMark rm;
  _file_stream->seek(_position, SEEK_SET);
  int begin_position = _position;
  u4 section_size = read_u4();
  int end_position = begin_position + section_size;

  u4 comp_order = read_u4();
  u1 compilation_type = read_u1();
  if (compilation_type != 0 && compilation_type != 1) {
    jprofilecache_log_error(jprofilecache, "[JitProfileCache] ERROR : illegal compilation type in JitProfile");
    _position = end_position;
    return NULL;
  }
  // parse method info
  const char* parse_method_name_char = read_string();
  logparse_illegal_check(parse_method_name_char, false, end_position);
  Symbol* method_name = create_symbol(parse_method_name_char);
  const char* parse_method_sig_char = read_string();
  logparse_illegal_check(parse_method_sig_char, false, end_position);
  Symbol* method_sig = create_symbol(parse_method_sig_char);
  u4 parse_first_invoke_init_order = read_u4();

  if ((int)parse_first_invoke_init_order == INVALID_FIRST_INVOKE_INIT_ORDER) {
    parse_first_invoke_init_order = this->info_holder()->chain()->length() - 1;
  }
  u4 parse_method_size = read_u4();
  u4 parse_method_hash = read_u4();
  int32_t parse_bci = (int32_t)read_u4();
  if (parse_bci != InvocationEntryBci) {
    logparse_illegal_count_check(parse_bci, false, end_position);
  }

  // parse class info
  const char* parse_class_name_char = read_string();
  logparse_illegal_check(parse_class_name_char, false, end_position);
  Symbol* class_name = create_symbol(parse_class_name_char);
  // ignore
  if (should_ignore_this_class(class_name)) {
    _position = end_position;
    return NULL;
  }
  const char* parse_class_loader_char = read_string();
  logparse_illegal_check(parse_class_loader_char, false, end_position);
  Symbol* class_loader = create_symbol(parse_class_loader_char);
  class_loader = JitProfileCacheUtils::remove_meaningless_suffix(class_loader);
  const char* path_char = read_string();
  logparse_illegal_check(path_char, false, end_position);
  Symbol* path = create_symbol(path_char);

  JProfileCacheClassDictionary* dict = this->info_holder()->jit_profile_cache_dict();
  unsigned int dict_hash = class_name->identity_hash();
  ProfileCacheClassEntry* entry = dict->find_head_entry(dict_hash, class_name);
  if (entry == NULL) {
    jprofilecache_log_info(jprofilecache, "[JitProfileCache] WARNING : class %s is missed in method parse", parse_class_name_char);
    _position = end_position;
    return NULL;
  }
  u4 parse_class_size = read_u4();
  u4 parse_class_crc32 = read_u4();
  u4 parse_class_hash = read_u4();

  // method counters info
  u4 parse_intp_invocation_count = read_u4();
  u4 parse_intp_throwout_count = read_u4();
  u4 parse_invocation_count = read_u4();
  u4 parse_backedge_count = read_u4();
  u1 compile_level = read_u1();

  int class_chain_offset = entry->chain_offset();
  ProfileCacheClassHolder* holder = entry->find_class_holder(parse_class_size, parse_class_crc32);
  if (holder == NULL) {
      holder = new ProfileCacheClassHolder(class_name, class_loader, path, parse_class_size, parse_class_hash, parse_class_crc32);
      entry->add_class_holder(holder);
  }
  ProfileCacheMethodHold* mh = new ProfileCacheMethodHold(method_name, method_sig);
  mh->set_interpreter_invocation_count(parse_intp_invocation_count);
  mh->set_interpreter_exception_count(parse_intp_throwout_count);
  mh->set_invocation_count(parse_invocation_count);
  mh->set_backage_count(parse_backedge_count);

  mh->set_compile_level(compile_level);
  mh->set_method_hash(parse_method_hash);
  mh->set_method_size(parse_method_size);

  parse_profile_data(mh);

  int method_chain_offset = class_chain_offset;
  mh->set_mounted_offset(method_chain_offset);
  this->info_holder()->chain()->add_method_at_index(mh, method_chain_offset);
  holder->add_method(mh);

  jprofilecache_log_debug(jprofilecache, "[JitProfileCache] method %s.%s is parsed successfully", parse_class_name_char, parse_method_name_char);

  return mh;
}

void JitProfileCacheFileParser::parse_profile_data(ProfileCacheMethodHold* mh) {
  u4 count = read_u4();
  GrowableArray<BytecodeProfileRecord*>* profile_list = mh->profile_list();
  while (count > 0) {
    BytecodeProfileRecord* bpr = read_data_layout();
    profile_list->append(bpr);
    count--;
  }
}
