/*
* Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
*
 */

#include "jprofilecache/jitProfileRecord.hpp"
#include "jprofilecache/jitProfileCache.hpp"
#include "jprofilecache/jitProfileCacheLog.hpp"
#include "runtime/arguments.hpp"
#include "runtime/globals.hpp"
#include "classfile/classLoaderData.hpp"
#include "classfile/classLoaderData.inline.hpp"
#include "jprofilecache/jitProfileCacheThread.hpp"

// define offset
#define PROFILECACHE_VERSION_OFFSET                  0
#define PROFILECACHE_MAGIC_NUMBER_OFFSET             4
#define FILE_SIZE_OFFSET                8
#define PROFILECACHE_CRC32_OFFSET                    12
#define APPID_OFFSET                    16
#define MAX_SYMBOL_LENGTH_OFFSET        20
#define RECORD_COUNT_OFFSET             24
#define PROFILECACHE_TIME_OFFSET                     28

#define HEADER_SIZE                     36

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
#define MAGIC_NUMBER                    0xBABA
#define RECORE_FILE_DEFAULT_NUMBER             0
#define RECORE_CRC32_DEFAULT_NUMBER            0

#define JVM_DEFINE_CLASS_PATH "_JVM_DefineClass_"

JitProfileRecorder::JitProfileRecorder()
        : _holder(NULL),
          _profilelog(NULL),
          _pos(0),
          _recorder_state(NOT_INIT),
          _class_init_list(NULL),
          _init_list_tail_node(NULL),
          _profile_record_dict(NULL),
          _class_init_order_num(-1),
          _flushed(false),
          _record_file_name(NULL),
          _max_symbol_length(0) {
}

JitProfileRecorder::~JitProfileRecorder() {
  if (!ProfilingCacheFile) {
    os::free((void*)logfile_name());
  }
  delete _class_init_list;
}

#define PROFILE_RECORDER_HT_SIZE  10240

void JitProfileRecorder::set_logfile_name(const char* name)  {
  _record_file_name = make_log_name(name, NULL);
}

#define PROFILECACHE_PID_BUFFER_SIZE  100
#define RECORD_MIN_LIMIT 0
#define RECORD_MAX_LINIT 3


void JitProfileRecorder::init() {
  assert(_recorder_state == NOT_INIT, "JitProfileRecorder state error");

  // check param
  if (!param_check()) {
    return;
  }
  
  // log file name
  if (ProfilingCacheFile == NULL) {
    char* buf = (char*)os::malloc(100, mtInternal);
    char fmt[] = "jprofilecache_%p.profile";
    Arguments::copy_expand_pid(fmt, sizeof(fmt), buf, PROFILECACHE_PID_BUFFER_SIZE);
    _record_file_name = buf;
  } else {
    set_logfile_name(ProfilingCacheFile);
    if (_record_file_name == NULL) {
      jprofilecache_log_error(profilecache)("[JitProfileCache] ERROR: file name check fail, file name is too long.");
      _recorder_state = IS_ERR;
      return;
    }
  }

  _class_init_list = new (ResourceObj::C_HEAP, mtInternal) LinkedListImpl<ClassSymbolEntry>();
  _profile_record_dict = new JitProfileRecordDictionary(PROFILE_RECORDER_HT_SIZE);
  _recorder_state = IS_OK;

  jprofilecache_log_debug(profilecache)("[JitProfileCache] DEBUG begin to collect, log file is %s", logfile_name());
}

bool JitProfileRecorder::param_check() {
  if (JProfilingCacheCompileAdvance) {
    jprofilecache_log_error(profilecache)("[JitProfileCache] ERROR: JProfilingCacheCompileAdvance and JProfilingCacheRecording cannot be enabled at the same time");
    _recorder_state = IS_ERR;
    return false;
  }
  if (!ProfileInterpreter) {
    jprofilecache_log_error(profilecache)("[JitProfileCache] ERROR: ProfileInterpreter must be enable");
    _recorder_state = IS_ERR;
    return false;
  }
  // disable class unloading
  if (ClassUnloading) {
    jprofilecache_log_error(profilecache)("[JitProfileCache] ERROR: ClassUnloading must be disable");
    _recorder_state = IS_ERR;
    return false;
  }
  if (UseConcMarkSweepGC) {
    if (FLAG_IS_DEFAULT(CMSClassUnloadingEnabled)) {
      FLAG_SET_DEFAULT(CMSClassUnloadingEnabled, false);
    }
    if (CMSClassUnloadingEnabled) {
      jprofilecache_log_error(profilecache)("[JitProfileCache] ERROR: if use CMS gc, CMSClassUnloadingEnabled must be disabled");
      _recorder_state = IS_ERR;
      return false;
    }
  }
  if (UseG1GC) {
    if (FLAG_IS_DEFAULT(ClassUnloadingWithConcurrentMark)) {
      FLAG_SET_DEFAULT(ClassUnloadingWithConcurrentMark, false);
    }
    if (ClassUnloadingWithConcurrentMark) {
      jprofilecache_log_error(profilecache)("[JitProfileCache] ERROR: if use G1 gc, ClassUnloadingWithConcurrentMark must be disabled");
      _recorder_state = IS_ERR;
      return false;
    }
  }
  // check class data sharing
  if (UseSharedSpaces) {
    jprofilecache_log_error(profilecache)("[JitProfileCache] ERROR: UseSharedSpaces must be disabled");
    _recorder_state = IS_ERR;
    return false;
  }
  // check CompilationProfileCacheRecordMinLevel
  if (CompilationProfileCacheRecordMinLevel < RECORD_MIN_LIMIT || CompilationProfileCacheRecordMinLevel > RECORD_MAX_LINIT) {
    jprofilecache_log_error(profilecache)("[JitProfileCache] ERROR: CompilationProfileCacheRecordMinLevel is invalid must be in the range: [0-3].");
    _recorder_state = IS_ERR;
    return false;
  }

  if (Arguments::mode() == Arguments::_int) {
    jprofilecache_log_error(profilecache)("[JitProfileCache] ERROR: when enable JProfilingCacheRecording, should not set -Xint");
    _recorder_state = IS_ERR;
    return false;
  }
  return true;
}

int JitProfileRecorder::assign_class_init_order(InstanceKlass* klass) {
  // ignore anonymous class
  if (klass->is_anonymous()) {
    return -1;
  }
  Symbol* record_name = klass->name();
  Symbol* record_path = klass->source_file_path();
  Symbol* record_loader_name = JitProfileCache::get_class_loader_name(klass->class_loader_data());
  if (record_name == NULL || record_name->utf8_length() == 0) {
      return -1;
  }
  MutexLockerEx mu(JitProfileRecorder_lock);
  if (_init_list_tail_node == NULL) {
    _class_init_list->add(ClassSymbolEntry(record_name, record_loader_name, record_path));
    _init_list_tail_node = _class_init_list->head();
  } else {
    _class_init_list->insert_after(ClassSymbolEntry(record_name, record_loader_name, record_path),
                                   _init_list_tail_node);
    _init_list_tail_node = _init_list_tail_node->next();
  }
  _class_init_order_num++;
#ifndef PRODUCT
  klass->set_initialize_order(_class_init_order_num);
#endif
  return _class_init_order_num;
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
  // free allocate memory
  for (int index = 0; index < table_size(); ++index) {
    for (JitProfileRecorderEntry* e = bucket(index); e != NULL;) {
      JitProfileRecorderEntry* to_remove = e;
      // read next before freeing.
      e = e->next();
      unlink_entry(to_remove);
      to_remove->free_allocate();
      FREE_C_HEAP_ARRAY(char, to_remove, mtInternal);
    }
  }
  assert(number_of_entries() == 0, "should have removed all entries");
  free_buckets();
  for (BasicHashtableEntry<mtInternal>* e = new_entry_free_list(); e != NULL; e = new_entry_free_list()) {
    ((JitProfileRecorderEntry*)e)->free_allocate();
    FREE_C_HEAP_ARRAY(char, e, mtInternal);
  }
}

JitProfileRecorderEntry* JitProfileRecordDictionary::new_entry(unsigned int hash, Method* method) {
  JitProfileRecorderEntry* entry = (JitProfileRecorderEntry*) new_entry_free_list();
  if (entry == NULL) {
    entry = (JitProfileRecorderEntry*) NEW_C_HEAP_ARRAY2(char, entry_size(), mtInternal, CURRENT_PC);
  }
  entry->set_next(NULL);
  entry->set_hash(hash);
  entry->set_literal(method);
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

  ConstMethod *const_method = method->constMethod();
  MethodCounters *method_counters = method->method_counters();
  InstanceKlass *klass = const_method->constants()->pool_holder();

  // record method info
  char *record_method_name = method->name()->as_C_string();
  char *method_name = (char*) malloc(strlen(record_method_name) + 1);
  memcpy(method_name, record_method_name, strlen(record_method_name) + 1);
  record_entry->set_method_name(method_name);

  char *record_method_sig = method->signature()->as_C_string();
  char *method_sig = (char*) malloc(strlen(record_method_sig) + 1);
  memcpy(method_sig, record_method_sig, strlen(record_method_sig) + 1);
  record_entry->set_method_sig(method_sig);
  // first invoke init order
  record_entry->set_first_invoke_init_order((u4) method->first_invoke_init_order());
  // bytecode size
  record_entry->set_method_code_size((u4) const_method->code_size());

#ifdef _LP64
  int record_method_hash = compute_universal_hash((char*)(const_method->code_base()), const_method->code_size());
  record_entry->set_method_hash((u4) record_method_hash);
#endif

  record_entry->set_method_bci((u4) bci);

  // record class info
  char *record_class_name = klass->name()->as_C_string();
  char *class_name = (char*) malloc(strlen(record_class_name) + 1);
  memcpy(class_name, record_class_name, strlen(record_class_name) + 1);
  Symbol *record_path_sym = klass->source_file_path();
  const char *record_path = NULL;
  if (record_path_sym != NULL) {
    record_path = record_path_sym->as_C_string();
    char *class_path = (char*) malloc(strlen(record_path) + 1);
    memcpy(class_path, record_path, strlen(record_path) + 1);
    record_entry->set_class_path(class_path);
  } else {
    record_path = JVM_DEFINE_CLASS_PATH;
    char *class_path = (char*) malloc(strlen(record_path) + 1);
    memcpy(class_path, record_path, strlen(record_path) + 1);
    record_entry->set_class_path(class_path);
  }
  oop record_class_loader = klass->class_loader();
  const char *record_loader_name = NULL;
  if (record_class_loader != NULL) {
    record_loader_name = record_class_loader->klass()->name()->as_C_string();
    char *class_load_name = (char*) malloc(strlen(record_loader_name) + 1);
    memcpy(class_load_name, record_loader_name, strlen(record_loader_name) + 1);
    record_entry->set_class_loader_name(class_load_name);
  } else {
    record_loader_name = "NULL";
    char *class_load_name = (char*) malloc(strlen(record_loader_name) + 1);
    memcpy(class_load_name, record_loader_name, strlen(record_loader_name) + 1);
    record_entry->set_class_loader_name(class_load_name);
  }
  record_entry->set_class_name(class_name);
  record_entry->set_class_bytes_size((u4)klass->bytes_size());
  record_entry->set_class_crc32((u4)klass->crc32());
  record_entry->set_class_number((u4)0x00);

  // record method counters
  if (method_counters != NULL) {
    record_entry->set_interpreter_invocation_count((u4)method_counters->interpreter_invocation_count());
    record_entry->set_interpreter_throwout_count((u4)method_counters->interpreter_throwout_count());
    record_entry->set_invocation_counter((u4)method_counters->invocation_counter()->raw_counter());
    record_entry->set_backedge_counter((u4)method_counters->backedge_counter()->raw_counter());
  } else {
    jprofilecache_log_warning(profilecache)("[JitProfileCache] WARNING : the method counter is NULL");
    record_entry->set_interpreter_invocation_count((u4)0);
    record_entry->set_interpreter_throwout_count((u4)0);
    record_entry->set_invocation_counter((u4)0);
    record_entry->set_backedge_counter((u4)0);
  }

  add_entry(target_bucket, record_entry);
  _count++;
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

  *(unsigned int*)header_buf = holder()->version();
  _pos += RECORE_VERSION_WIDTH;
  offset += RECORE_VERSION_WIDTH;

  *(unsigned int*)((char*)header_buf + offset) = MAGIC_NUMBER;
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

  *(unsigned jlong*)((char*)header_buf + offset) = os::javaTimeMillis();
  _pos += RECORE_TIME_WIDTH;
  offset += RECORE_TIME_WIDTH;

  _profilelog->write(header_buf, offset);
}

void JitProfileRecorder::write_inited_class() {
  assert(_profilelog->is_open(), "log file must be opened");
  ResourceMark rm;
  unsigned int begin_position = _pos;
  unsigned int size_anchor = begin_position;

  write_u4((u4)MAGIC_NUMBER);
  write_u4((u4)class_init_count());

  int cnt = 0;
  const LinkedListNode<ClassSymbolEntry>* node = class_init_list()->head();
  while (node != NULL) {
    const ClassSymbolEntry* record_entry = node->peek();
    char* record_class_name = record_entry->class_name()->as_C_string();
    const char* record_class_loader_name = NULL;
    if (record_entry->class_loader_name() == NULL) {
      record_class_loader_name = "NULL";
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
    node = node->next();
    cnt++;
  }
  assert(cnt == class_init_count(), "error happened in profile info record");
  unsigned int end_position = _pos;
  unsigned int section_size = end_position - begin_position;
  overwrite_u4(section_size, size_anchor);
}

void JitProfileRecorder::write_profilecache_record(JitProfileRecorderEntry* entry, int bci, int order) {
  ResourceMark rm;
  unsigned int begin_position = _pos;
  unsigned int total_size = 0;

  unsigned int size_anchor = begin_position;
  write_u4((u4)MAGIC_NUMBER);
  write_u4((u4)order);

  // record compilation type
  u1 compilation_type = bci == -1 ? 0 : 1;
  write_u1(compilation_type);

  // record method info
  record_method_info(entry, bci);

  // record class info
  record_class_info(entry);

  write_u4(entry->get_interpreter_invocation_count());
  write_u4(entry->get_interpreter_throwout_count());
  write_u4(entry->get_invocation_counter());
  write_u4(entry->get_backedge_counter());


  unsigned int end_position = _pos;
  unsigned int section_size = end_position - begin_position;
  overwrite_u4(section_size, size_anchor);
}

void JitProfileRecorder::record_class_info(JitProfileRecorderEntry* entry) {
  const char* record_class_name = entry->get_class_name();
  const char* loader_name = entry->get_class_loader_name();
  const char* record_path = entry->get_class_path();
  
  write_string(record_class_name, strlen(record_class_name));
  write_string(loader_name, strlen(loader_name));
  write_string(record_path, strlen(record_path));
  write_u4((u4)entry->get_class_bytes_size());
  write_u4((u4)entry->get_class_crc32());
  write_u4((u4)0x00);
}

void JitProfileRecorder::record_method_info(JitProfileRecorderEntry* entry, int bci) {
  const char* record_method_name = entry->get_method_name();
  write_string(record_method_name, strlen(record_method_name));
  const char* record_method_sig = entry->get_method_sig();
  write_string(record_method_sig, strlen(record_method_sig));
  // first invoke init order
  write_u4((u4) entry->get_first_invoke_init_order());
  // bytecode size
  write_u4((u4) entry->get_method_code_size());
  write_u4((u4) entry->get_method_hash());
  write_u4((u4) entry->get_method_bci());
}

void JitProfileRecorder::write_profilecache_footer() {
}

void JitProfileRecorder::flush_record() {
  MutexLockerEx mu(JitProfileRecorder_lock);
  if (!is_valid() || is_flushed()) {
    return;
  }
  set_flushed(true);

  // set log permission
  int fd = open(logfile_name(), O_CREAT, S_IRUSR | S_IWUSR);
  if (fd < 0) {
    jprofilecache_log_error(profilecache)("[JitProfileCache] ERROR : open log file fail! path is %s", logfile_name());
    return;
  }
  close(fd);

  // open randomAccessFileStream
  _profilelog = new (ResourceObj::C_HEAP, mtInternal) randomAccessFileStream(logfile_name(), "wb+");
  if (_profilelog == NULL || !_profilelog->is_open()) {
    jprofilecache_log_error(profilecache)("[JitProfileCache] ERROR : open log file fail! path is %s", logfile_name());
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
      write_profilecache_record(entry, entry->bci(), entry->order());
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
  // close fd
  delete _profilelog;
  _profilelog = NULL;

  delete _profile_record_dict;
  _profile_record_dict = NULL;

  jprofilecache_log_info(profilecache)("[JitProfileCache] Profile information output completed. File: %s", logfile_name() == NULL ? "NULL" : logfile_name());
}