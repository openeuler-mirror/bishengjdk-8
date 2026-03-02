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
#include "classfile/classLoader.hpp"
#include "jprofilecache/jitProfileCacheFileParser.hpp"
#include "jprofilecache/jitProfileCacheUtils.hpp"
#include "jprofilecache/jitProfileRecord.hpp"
#include "libadt/dict.hpp"
#include "jprofilecache/jitProfileCacheLog.hpp"
#include "runtime/arguments.hpp"
#include "runtime/globals.hpp"
#include "runtime/mutexLocker.hpp"
#include "code/nmethod.hpp"

// define offset
#define PROFILECACHE_VERSION_OFFSET                  0
#define PROFILECACHE_MAGIC_NUMBER_OFFSET             4
#define FILE_SIZE_OFFSET                             8
#define PROFILECACHE_CRC32_OFFSET                    12
#define APPID_OFFSET                                 16
#define MAX_SYMBOL_LENGTH_OFFSET                     20
#define RECORD_COUNT_OFFSET                          24
#define PROFILECACHE_TIME_OFFSET                     28

#define HEADER_SIZE                                  36

// define width
#define RECORE_VERSION_WIDTH (PROFILECACHE_MAGIC_NUMBER_OFFSET - PROFILECACHE_VERSION_OFFSET)
#define RECORE_MAGIC_WIDTH (FILE_SIZE_OFFSET - PROFILECACHE_MAGIC_NUMBER_OFFSET)
#define FILE_SIZE_WIDTH (PROFILECACHE_CRC32_OFFSET - FILE_SIZE_OFFSET)
#define RECORE_CRC32_WIDTH (APPID_OFFSET - PROFILECACHE_CRC32_OFFSET)
#define RECORE_APPID_WIDTH (MAX_SYMBOL_LENGTH_OFFSET - APPID_OFFSET)
#define RECORE_MAX_SYMBOL_LENGTH_WIDTH (RECORD_COUNT_OFFSET - MAX_SYMBOL_LENGTH_OFFSET)
#define RECORD_COUNTS_WIDTH (PROFILECACHE_TIME_OFFSET - RECORD_COUNT_OFFSET)
#define RECORE_TIME_WIDTH (HEADER_SIZE - PROFILECACHE_TIME_OFFSET)

// default value
#define RECORE_FILE_DEFAULT_NUMBER             0
#define RECORE_CRC32_DEFAULT_NUMBER            0

#define JVM_DEFINE_CLASS_PATH "_JVM_DefineClass_"

// auto jprofile
#define AUTO_TEMP_JPCFILE_NAME "jprofilecache.profile.tmp"
#define AUTO_JPCFILE_NAME      "jprofilecache.profile"

const char* JitProfileRecorder::_auto_jpcfile_name = NULL;
const char* JitProfileRecorder::_auto_temp_jpcfile_name = NULL;
FILE*       JitProfileRecorder::_auto_jpcfile_filepointer = NULL;

JitProfileRecorder::JitProfileRecorder():
          _max_symbol_length(0),
          _pos(0),
          _class_init_order_num(-1),
          _flushed(false),
          _record_file_name(NULL),
          _profilelog(NULL),
          _recorder_state(NOT_INIT),
          _class_init_list(NULL),
          _init_list_tail_node(NULL),
          _class_init_nodes(NULL),
          _profile_record_dict(NULL){}

JitProfileRecorder::~JitProfileRecorder() {
  if (!ProfilingCacheFile) {
    os::free((void*)logfile_name());
  }
  delete _class_init_nodes;
  delete _class_init_list;
}

#define PROFILE_RECORDER_HT_SIZE  10240

void JitProfileRecorder::set_logfile_name(const char* name)  {
  _record_file_name = make_log_name(name, NULL);
}

void JitProfileRecorder::set_jpcfile_filepointer(FILE* file) {
  _auto_jpcfile_filepointer = file;
}

const char* JitProfileRecorder::auto_jpcfile_name() {
  if (_auto_jpcfile_name == NULL) {
    _auto_jpcfile_name = make_log_name(AUTO_JPCFILE_NAME, JProfilingCacheAutoArchiveDir);
  }
  return _auto_jpcfile_name;
}

const char* JitProfileRecorder::auto_temp_jpcfile_name() {
  if (_auto_temp_jpcfile_name == NULL) {
    _auto_temp_jpcfile_name = make_log_name(AUTO_TEMP_JPCFILE_NAME, JProfilingCacheAutoArchiveDir);
  }
  return _auto_temp_jpcfile_name;
}

#define PROFILECACHE_PID_BUFFER_SIZE  100

void JitProfileRecorder::init() {
  assert(_recorder_state == NOT_INIT, "JitProfileRecorder state error");
  if (JProfilingCacheCompileAdvance) {
    jprofilecache_log_error(jprofilecache, "[JitProfileCache] ERROR: JProfilingCacheCompileAdvance and JProfilingCacheRecording cannot be enabled at the same time");
    _recorder_state = IS_ERR;
    return;
  }
  if (!ProfileInterpreter) {
    jprofilecache_log_error(jprofilecache, "[JitProfileCache] ERROR: ProfileInterpreter must be enable");
    _recorder_state = IS_ERR;
    return;
  }
  // disable class unloading
  if (ClassUnloading) {
    jprofilecache_log_error(jprofilecache, "[JitProfileCache] ERROR: ClassUnloading must be disabled");
    _recorder_state = IS_ERR;
    return;
  }

  if (UseG1GC && ClassUnloadingWithConcurrentMark) {
    jprofilecache_log_error(jprofilecache, "[JitProfileCache] ERROR: if use G1 gc, ClassUnloadingWithConcurrentMark must be disabled");
    _recorder_state = IS_ERR;
    return;
  }

  // profile file name
  if (JProfilingCacheAutoArchiveDir != NULL) {
    _record_file_name = auto_temp_jpcfile_name();
  } else if (ProfilingCacheFile == NULL) {
    char* buf = (char*)os::malloc(100, mtInternal);
    char fmt[] = "jprofilecache_%p.profile";
    Arguments::copy_expand_pid(fmt, sizeof(fmt), buf, PROFILECACHE_PID_BUFFER_SIZE);
    _record_file_name = buf;
  } else {
    set_logfile_name(ProfilingCacheFile);
  }

  _class_init_list = new (ResourceObj::C_HEAP, mtInternal) LinkedListImpl<ClassSymbolEntry>();
  _class_init_nodes = new (ResourceObj::C_HEAP, mtInternal)
      GrowableArray<LinkedListNode<ClassSymbolEntry>*>(128, mtInternal);
  _profile_record_dict = new JitProfileRecordDictionary(PROFILE_RECORDER_HT_SIZE);
  _recorder_state = IS_OK;

  jprofilecache_log_debug(jprofilecache, "[JitProfileCache] begin to collect, log file is %s", logfile_name());
}

int JitProfileRecorder::assign_class_init_order(InstanceKlass* klass) {
  // ignore anonymous class
  if (klass->is_anonymous()) {
    return -1;
  }
  Symbol* record_name = klass->name();
  Symbol* record_path = klass->source_file_path();
  Symbol* record_loader_name = JitProfileCacheUtils::get_class_loader_name(klass->class_loader_data());
  if (record_name == NULL || record_name->utf8_length() == 0) {
    return -1;
  }
  MutexLockerEx mu(JitProfileRecorder_lock, Mutex::_no_safepoint_check_flag);
  LinkedListNode<ClassSymbolEntry>* node = NULL;
  if (_init_list_tail_node == NULL) {
    node = _class_init_list->add(ClassSymbolEntry(record_name, record_loader_name, record_path));
    _init_list_tail_node = node;
  } else {
    node = _class_init_list->insert_after(ClassSymbolEntry(record_name, record_loader_name, record_path),
                                          _init_list_tail_node);
    _init_list_tail_node = node;
  }
  if (node == NULL) {
    return -1;
  }
  _class_init_nodes->append(node);
  _class_init_order_num++;
#ifndef PRODUCT
  klass->set_initialize_order(_class_init_order_num);
#endif
  return _class_init_order_num;
}

void JitProfileRecorder::mark_class_init_result(int init_order, bool success) {
  if (init_order < 0) {
    return;
  }
  MutexLockerEx mu(JitProfileRecorder_lock, Mutex::_no_safepoint_check_flag);
  if (_class_init_nodes == NULL || init_order >= _class_init_nodes->length()) {
    return;
  }
  LinkedListNode<ClassSymbolEntry>* node = _class_init_nodes->at(init_order);
  if (node != NULL) {
    node->data()->set_clinit_succeeded(success);
  }
}

void JitProfileRecorder::add_method(Method* method, int method_bci) {
  MutexLockerEx mu(JitProfileRecorder_lock, Mutex::_no_safepoint_check_flag);
  // if is flushed, stop adding method
  if (is_flushed()) {
    return;
  }
  // not deal with OSR Compilation
  if (method_bci != InvocationEntryBci) {
    return;
  }
  assert(is_valid(), "JProfileCache state must be OK");
  unsigned int hash = compute_hash(method);
  dict()->add_method(hash, method, method_bci);
}

void JitProfileRecorder::update_max_symbol_length(int len) {
  if (len > _max_symbol_length) {
    _max_symbol_length = len;
  }
}

JitProfileRecordDictionary::JitProfileRecordDictionary(unsigned int size)
  : Hashtable<Method*, mtInternal>(size, sizeof(JitProfileRecorderEntry)),
    _count(0) {
  // do nothing
}

JitProfileRecordDictionary::~JitProfileRecordDictionary() {
  free_buckets();
}

JitProfileRecorderEntry* JitProfileRecordDictionary::new_entry(unsigned int hash, Method* method) {
  JitProfileRecorderEntry* entry = (JitProfileRecorderEntry*)Hashtable<Method*, mtInternal>::new_entry(hash, method);
  entry->init();
  return entry;
}

JitProfileRecorderEntry* JitProfileRecordDictionary::add_method(unsigned int method_hash, Method* method, int bci) {
  assert_lock_strong(JitProfileRecorder_lock);
  int target_bucket = hash_to_index(method_hash);
  JitProfileRecorderEntry* record_entry = find_entry(method_hash, method);
  if (record_entry != NULL) {
    return record_entry;
  }
  // add method entry
  record_entry = new_entry(method_hash, method);
  record_entry->set_bci(bci);
  record_entry->set_order(count());
  add_entry(target_bucket, record_entry);
  _count++;

  jprofilecache_log_debug(jprofilecache, "[JitProfileCache] Record method %s", method->name_and_sig_as_C_string());

  return record_entry;
}

JitProfileRecorderEntry* JitProfileRecordDictionary::find_entry(unsigned int hash, Method* method) {
  int index = hash_to_index(hash);
  for (JitProfileRecorderEntry* p = bucket(index); p != NULL; p = p->next()) {
    if (p->literal() == method) {
      return p;
    }
  }
  return NULL;
}

void JitProfileRecordDictionary::free_entry(JitProfileRecorderEntry* entry) {
  Hashtable<Method*, mtInternal>::free_entry(entry);
}

#define WRITE_U1_INTERVAL 1
#define WRITE_U4_INTERVAL 4
#define OVERWRITE_U4_INTERVAL 4

static char record_buf[12];
void JitProfileRecorder::write_u1(u1 value) {
  *(u1*)record_buf = value;
  _profilelog->write(record_buf, WRITE_U1_INTERVAL);
  _pos += WRITE_U1_INTERVAL;
}

void JitProfileRecorder::write_u4(u4 value) {
  *(u4*)record_buf = value;
  _profilelog->write(record_buf, WRITE_U4_INTERVAL);
  _pos += WRITE_U4_INTERVAL;
}

void JitProfileRecorder::write_data_layout(ProfileData* value) {
  int size = value->size_in_bytes();
  write_u4(size);
  _profilelog->write((char*)value->dp(), size);
  _pos += size;
}

void JitProfileRecorder::overwrite_u4(u4 value, unsigned int offset) {
  *(u4*)record_buf = value;
  _profilelog->write(record_buf, OVERWRITE_U4_INTERVAL, offset);
}

void JitProfileRecorder::write_string(const char* src, size_t len) {
  assert(src != NULL && len != 0, "empty string is not allowed");
  _profilelog->write(src, len);
  _profilelog->write("\0", 1);
  _pos += len + 1;
  update_max_symbol_length((int)len);
}

#define JVM_DEFINE_CLASS_PATH "_JVM_DefineClass_"

#define CRC32_BUF_SIZE   1024
static char crc32_buf[CRC32_BUF_SIZE];

int JitProfileRecorder::compute_crc32(randomAccessFileStream* fileStream) {
  long old_position = (long)fileStream->tell();
  fileStream->seek(HEADER_SIZE, SEEK_SET);
  int content_size = fileStream->fileSize() - HEADER_SIZE;
  assert(content_size > 0, "sanity check");
  int loops = content_size / CRC32_BUF_SIZE;
  int partial_chunk_size = content_size % CRC32_BUF_SIZE;
  int crc = 0;

  for (int i = 0; i < loops; ++i) {
    fileStream->read(crc32_buf, CRC32_BUF_SIZE, 1);
    crc = ClassLoader::crc32(crc, crc32_buf, CRC32_BUF_SIZE);
  }
  if (partial_chunk_size > 0) {
    fileStream->read(crc32_buf, partial_chunk_size, 1);
    crc = ClassLoader::crc32(crc, crc32_buf, partial_chunk_size);
  }
  fileStream->seek(old_position, SEEK_SET);

  return crc;
}
#undef CRC32_BUF_SIZE

static char header_buf[HEADER_SIZE];
void JitProfileRecorder::write_profilecache_header() {
  assert(_profilelog->is_open(), "");

  size_t offset = 0;

  *(unsigned int*)header_buf = version();
  _pos += RECORE_VERSION_WIDTH;
  offset += RECORE_VERSION_WIDTH;

  *(unsigned int*)((char*)header_buf + offset) = JPROFILECACHE_MAGIC_NUMBER;
  _pos += RECORE_MAGIC_WIDTH;
  offset += RECORE_MAGIC_WIDTH;

  *(unsigned int*)((char*)header_buf + offset) = RECORE_FILE_DEFAULT_NUMBER;
  _pos += RECORE_CRC32_WIDTH;
  offset += RECORE_CRC32_WIDTH;

  *(unsigned int*)((char*)header_buf + offset) = RECORE_CRC32_DEFAULT_NUMBER;
  _pos += RECORE_CRC32_WIDTH;
  offset += RECORE_CRC32_WIDTH;

  *(unsigned int*)((char*)header_buf + offset) = CompilationProfileCacheAppID;
  _pos += RECORE_APPID_WIDTH;
  offset += RECORE_APPID_WIDTH;

  *(unsigned int*)((char*)header_buf + offset) = 0;
  _pos += RECORE_MAX_SYMBOL_LENGTH_WIDTH;
  offset += RECORE_MAX_SYMBOL_LENGTH_WIDTH;

  *(unsigned int*)((char*)header_buf + offset) = recorded_count();
  _pos += RECORD_COUNTS_WIDTH;
  offset += RECORD_COUNTS_WIDTH;

  *(jlong*)((char*)header_buf + offset) = os::javaTimeMillis();
  _pos += RECORE_TIME_WIDTH;
  offset += RECORE_TIME_WIDTH;

  _profilelog->write(header_buf, offset);
}

void JitProfileRecorder::write_inited_class() {
  assert(_profilelog->is_open(), "log file must be opened");
  ResourceMark rm;
  unsigned int begin_position = _pos;
  unsigned int size_anchor = begin_position;

  write_u4((u4)JPROFILECACHE_MAGIC_NUMBER);
  write_u4((u4)class_init_count());

  int cnt = 0;
  int success_cnt = 0;
  const LinkedListNode<ClassSymbolEntry>* node = class_init_list()->head();
  while (node != NULL) {
    const ClassSymbolEntry* record_entry = node->peek();
    char* record_class_name = record_entry->class_name()->as_C_string();
    const char* record_class_loader_name = NULL;
    if (record_entry->class_loader_name() == NULL) {
      record_class_loader_name = "nullptr";
    } else {
      record_class_loader_name = record_entry->class_loader_name()->as_C_string();
    }
    const char* path = NULL;
    if (record_entry->path() == NULL) {
      path = JVM_DEFINE_CLASS_PATH;
    } else {
      path = record_entry->path()->as_C_string();
    }
    write_string(record_class_name, strlen(record_class_name));
    write_string(record_class_loader_name, strlen(record_class_loader_name));
    write_string(path, strlen(path));
    write_u1(record_entry->clinit_succeeded() ? (u1)1 : (u1)0);
    if (record_entry->clinit_succeeded()) {
      success_cnt++;
    }
    node = node->next();
    cnt++;
  }
  assert(cnt == class_init_count(), "error happened in profile info record");
  jprofilecache_log_info(jprofilecache,
                         "[JitProfileCache] class init records total=%d success=%d failed=%d",
                         cnt, success_cnt, cnt - success_cnt);
  unsigned int end_position = _pos;
  unsigned int section_size = end_position - begin_position;
  overwrite_u4(section_size, size_anchor);
}

void JitProfileRecorder::write_profilecache_record(Method* method, int bci, int order) {
  ResourceMark rm;
  unsigned int begin_position = _pos;
  unsigned int total_size = 0;
  ConstMethod* const_method = method->constMethod();
  MethodCounters* method_counters = method->method_counters();
  InstanceKlass* klass = const_method->constants()->pool_holder();

  unsigned int size_anchor = begin_position;
  write_u4((u4)JPROFILECACHE_MAGIC_NUMBER);
  write_u4((u4)order);

  // record compilation type
  u1 compilation_type = bci == -1 ? 0 : 1;
  write_u1(compilation_type);

  // record method info
  record_method_info(method, const_method, bci);

  // record class info
  record_class_info(klass);

  // record method counters
  if (method_counters != NULL) {
    write_u4((u4)method->interpreter_invocation_count());
    write_u4((u4)method_counters->interpreter_throwout_count());
    write_u4((u4)method_counters->invocation_counter()->raw_counter());
    write_u4((u4)method_counters->backedge_counter()->raw_counter());
  } else {
    jprofilecache_log_warning(jprofilecache, "[JitProfileCache] WARNING: the method counter is nullptr");
    write_u4((u4)0);
    write_u4((u4)0);
    write_u4((u4)0);
    write_u4((u4)0);
  }

  // write compile level
  write_u1((u1)method->highest_comp_level());

  write_method_profiledata(method->method_data());

  unsigned int end_position = _pos;
  unsigned int section_size = end_position - begin_position;
  overwrite_u4(section_size, size_anchor);
}

bool JitProfileRecorder::is_recordable_data(ProfileData* dp) {
  return dp->is_BranchData() || dp->is_MultiBranchData();
}

ArgInfoData * JitProfileRecorder::get_ArgInfoData(MethodData* mdo) {
  DataLayout* dp    = mdo->extra_data_base();
  DataLayout* end   = mdo->extra_data_limit();
  for (; dp < end; dp = MethodData::next_extra(dp)) {
    if (dp->tag() == DataLayout::arg_info_data_tag)
      return new ArgInfoData(dp);
  }
  return NULL;
}

void JitProfileRecorder::write_method_profiledata(MethodData* mdo) {
  if (mdo == NULL) {
    write_u4((u4)0);
    return;
  }
  ProfileData* dp_src = mdo->first_data();
  int count = 0;
  for (; mdo->is_valid(dp_src) ;dp_src = mdo->next_data(dp_src)) {
    if (is_recordable_data(dp_src)) {
      count++;
    }
  }
  ArgInfoData * arg_info = get_ArgInfoData(mdo);
  if (arg_info != NULL) { count++; }
  write_u4((u4)count);
  if (arg_info != NULL) {
    write_data_layout(arg_info);
    jprofilecache_log_info(jprofilecache, "Record ArgInfoData of method: %s",
          mdo->method()->name_and_sig_as_C_string());
  }
  for (dp_src = mdo->first_data(); mdo->is_valid(dp_src) ;dp_src = mdo->next_data(dp_src)) {
    if (is_recordable_data(dp_src)) {
      DataLayout* dp = (DataLayout*)dp_src->dp();
      write_data_layout(dp_src);
      jprofilecache_log_info(jprofilecache, "Record ProfileData(Tag:%d) on bytecode(%d) of method: %s",
          dp->tag(), dp_src->bci(), mdo->method()->name_and_sig_as_C_string());
    }
  }
}

void JitProfileRecorder::record_class_info(InstanceKlass* klass) {
  char* record_class_name = klass->name()->as_C_string();
  Symbol* record_path_sym = klass->source_file_path();
  const char* record_path = NULL;
  if (record_path_sym != NULL) {
    record_path = record_path_sym->as_C_string();
  } else {
    record_path = JVM_DEFINE_CLASS_PATH;
  }
  oop record_class_loader = klass->class_loader();
  const char* loader_name = NULL;
  if (record_class_loader != NULL) {
    loader_name = record_class_loader->klass()->name()->as_C_string();
  } else {
    loader_name = "nullptr";
  }
  write_string(record_class_name, strlen(record_class_name));
  write_string(loader_name, strlen(loader_name));
  write_string(record_path, strlen(record_path));
  write_u4((u4)klass->bytes_size());
  write_u4((u4)klass->crc32());
  write_u4((u4)0x00);
}

void JitProfileRecorder::record_method_info(Method *method, ConstMethod* const_method, int bci) {
  char* record_method_name = method->name()->as_C_string();
  write_string(record_method_name, strlen(record_method_name));
  char* record_method_sig = method->signature()->as_C_string();
  write_string(record_method_sig, strlen(record_method_sig));
  // first invoke init order
  write_u4((u4)method->first_invoke_init_order());
  // bytecode size
  write_u4((u4)const_method->code_size());

#ifdef _LP64
  int record_method_hash = compute_universal_hash((char *)(const_method->code_base()), const_method->code_size());
  write_u4((u4)record_method_hash);
  write_u4((u4)bci);

#endif
}

void JitProfileRecorder::write_profilecache_footer() {
}

void JitProfileRecorder::flush_record() {
  MutexLocker mu(JitProfileRecorder_lock);
  if (!is_valid() || is_flushed()) {
    return;
  }
  set_flushed(true);

  // open randomAccessFileStream
  if (JProfilingCacheAutoArchiveDir != NULL) {
    _profilelog = new (ResourceObj::C_HEAP, mtInternal) randomAccessFileStream(_auto_jpcfile_filepointer);
  } else {
    int fd = open(logfile_name(), O_CREAT, S_IRUSR | S_IWUSR);
    if (fd < 0) {
      jprofilecache_log_error(jprofilecache, "[JitProfileCache] ERROR : open log file fail! path is %s", logfile_name());
      _recorder_state = IS_ERR;
      return;
    }
    close(fd);

    _profilelog = new (ResourceObj::C_HEAP, mtInternal) randomAccessFileStream(logfile_name(), "wb+");
  }

  if (_profilelog == NULL || !_profilelog->is_open()) {
    jprofilecache_log_error(jprofilecache, "[JitProfileCache] ERROR : open log file fail! path is %s", logfile_name());
    _recorder_state = IS_ERR;
    return;
  }

  // head section
  write_profilecache_header();
  // write class init section
  write_inited_class();
  // write method profile info
  for (int index = 0; index < dict()->table_size(); index++) {
    for (JitProfileRecorderEntry* entry = dict()->bucket(index);
         entry != NULL;
         entry = entry->next()) {
      write_profilecache_record(entry->literal(), entry->bci(), entry->order());
    }
  }
  // foot section
  write_profilecache_footer();

  // set file size
  overwrite_u4((u4)_pos, FILE_SIZE_OFFSET);
  // set max symbol length
  overwrite_u4((u4)_max_symbol_length, MAX_SYMBOL_LENGTH_OFFSET);
  // compute and set file's crc32
  int crc32 = JitProfileRecorder::compute_crc32(_profilelog);
  overwrite_u4((u4)crc32, PROFILECACHE_CRC32_OFFSET);

  _profilelog->flush();

  if (JProfilingCacheAutoArchiveDir != NULL) {
    int res = ::rename(logfile_name(), auto_jpcfile_name());
    if (res != 0) {
      delete _profilelog;
      _profilelog = NULL;
      ::unlink(logfile_name());
      jprofilecache_log_error(jprofilecache, "[JitProfileCache] Autogenerate jprofilecache file failed to rename!");
      _recorder_state = IS_ERR;
      return;
    }
  }

  // Auto jprofile makes a temp file to record. When recording is completed,
  // temp file needs to rename to real jprofile filename and unlock.
  // close fd （also unlock file）
  delete _profilelog;
  _profilelog = NULL;

  jprofilecache_log_info(jprofilecache, "[JitProfileCache] Profile information output completed. File: %s", logfile_name());
}
