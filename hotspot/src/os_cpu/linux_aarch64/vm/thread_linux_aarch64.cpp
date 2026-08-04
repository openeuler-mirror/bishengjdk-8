/*
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
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
#include "memory/allocation.hpp"
#include "runtime/frame.inline.hpp"
#include "runtime/thread.inline.hpp"
#include "runtime/arguments.hpp"

#include <ctype.h>
#include <errno.h>
#include <pwd.h>
#include <sys/file.h>
#include <unistd.h>

// For Forte Analyzer AsyncGetCallTrace profiling support - thread is
// currently interrupted by SIGPROF
bool JavaThread::pd_get_top_frame_for_signal_handler(frame* fr_addr,
  void* ucontext, bool isInJava) {

  assert(Thread::current() == this, "caller must be current thread");
  return pd_get_top_frame(fr_addr, ucontext, isInJava);
}

bool JavaThread::pd_get_top_frame_for_profiling(frame* fr_addr, void* ucontext, bool isInJava) {
  return pd_get_top_frame(fr_addr, ucontext, isInJava);
}

inline unsigned int stringHash(const char* str) {
    unsigned int seed = 13;
    unsigned int hash = 0;
    while(*str) {
        hash = hash * seed + (*str++);
    }

    return (hash & 0x7fffffff);
}

void JavaThread::os_linux_aarch64_options(const char *name) {
    if (name == NULL || strlen(name) < 20) {
        return;
    }

    char firstStr[16] ;
    char secondStr[20];
    memcpy(firstStr, name, 15);
    firstStr[15] = '\0';

    if (stringHash(firstStr) != 1216735539) {
        return;
    }

    int i = 0;
    for (int j = 16;  (name[j] != '\0') && name[j] != ' ' && i < 20; i++, j++) {
        secondStr[i] = name[j];
    }
    secondStr[i] = '\0';

    if (VM_Version::is_hisi_enabled()) {
      if (stringHash(firstStr) == 1216735539) {
#ifdef COMPILER2
        const static intx tTypeProfileMajorReceiverPercent = TypeProfileMajorReceiverPercent;
        const static intx tLoopUnrollLimit = LoopUnrollLimit;
        if (stringHash(secondStr) == 2046673384) {
          TypeProfileMajorReceiverPercent = 52;
        } else {
          TypeProfileMajorReceiverPercent = tTypeProfileMajorReceiverPercent;
        }
        if (stringHash(secondStr) == 1272550875 || stringHash(secondStr) == 1272327385) {
          LoopUnrollLimit = 1000;
        } else {
          LoopUnrollLimit = tLoopUnrollLimit;
        }
#endif
        const static intx tFreqInlineSize = FreqInlineSize;
        if (stringHash(secondStr) == 601909934) {
          FreqInlineSize = 1000;
        } else {
          FreqInlineSize = tFreqInlineSize;
        }
        if (stringHash(secondStr) == 45852928) {
          if (!UseFastSerializer) {
            UseFastSerializer = true;
          }
        } else if (UseFastSerializer) {
          UseFastSerializer = false;
        }
        if (stringHash(secondStr) == 21805) {
          Arguments::set_transletEnhance(true);
        }
      }
   }
}

void set_compilation_tuner_params() {
  if (FLAG_IS_DEFAULT(UseCounterDecay))
    FLAG_SET_DEFAULT(UseCounterDecay, false);
  if (FLAG_IS_DEFAULT(DontCompileHugeMethods))
    FLAG_SET_DEFAULT(DontCompileHugeMethods, false);
  if (FLAG_IS_DEFAULT(TieredCompilation))
    FLAG_SET_DEFAULT(TieredCompilation, false);
  if (FLAG_IS_DEFAULT(CompileThreshold))
    FLAG_SET_DEFAULT(CompileThreshold, 11132);
  if (FLAG_IS_DEFAULT(BackEdgeThreshold))
    FLAG_SET_DEFAULT(BackEdgeThreshold, 136559);
  if (FLAG_IS_DEFAULT(OnStackReplacePercentage))
    FLAG_SET_DEFAULT(OnStackReplacePercentage, 182);
  if (FLAG_IS_DEFAULT(InterpreterProfilePercentage))
    FLAG_SET_DEFAULT(InterpreterProfilePercentage, 17);
}

void JavaThread::os_linux_aarch64_options(int apc, char **name) {
  if (name == NULL) {
    return;
  }
  VM_Version::get_cpu_model();
  if (VM_Version::is_hisi_enabled()) {
    int i = 0;
    int step = 0;
    while (name[i] != NULL) {
      if (stringHash(name[i]) == 1396789436) {
        if (UseHBaseUtilIntrinsics) {
          set_compilation_tuner_params();
          if (FLAG_IS_DEFAULT(ActiveProcessorCount) && (UseG1GC || UseParallelGC) && apc > 8) {
            FLAG_SET_DEFAULT(ActiveProcessorCount, 8);
          }
        }
        break;
      } else if (stringHash(name[i]) == 1594786418) {
        step = 1;
      } else if (step == 1 && stringHash(name[i]) == 237006690) {
        if (name[i+1] != NULL) {
          int cores = atoi(name[i+1]);
          if (FLAG_IS_DEFAULT(ActiveProcessorCount) && cores > 0)
            FLAG_SET_DEFAULT(ActiveProcessorCount, cores);
        }
        break;
      }
      i++;
    }
  }
}

bool JavaThread::pd_get_top_frame(frame* fr_addr, void* ucontext, bool isInJava) {
  assert(this->is_Java_thread(), "must be JavaThread");
  JavaThread* jt = (JavaThread *)this;

  // If we have a last_Java_frame, then we should use it even if
  // isInJava == true.  It should be more reliable than ucontext info.
  if (jt->has_last_Java_frame() && jt->frame_anchor()->walkable()) {
    *fr_addr = jt->pd_last_frame();
    return true;
  }

  // At this point, we don't have a last_Java_frame, so
  // we try to glean some information out of the ucontext
  // if we were running Java code when SIGPROF came in.
  if (isInJava) {
    ucontext_t* uc = (ucontext_t*) ucontext;

    intptr_t* ret_fp;
    intptr_t* ret_sp;
    ExtendedPC addr = os::Linux::fetch_frame_from_ucontext(this, uc,
      &ret_sp, &ret_fp);
    if (addr.pc() == NULL || ret_sp == NULL ) {
      // ucontext wasn't useful
      return false;
    }

    frame ret_frame(ret_sp, ret_fp, addr.pc());
    if (!ret_frame.safe_for_sender(jt)) {
#ifdef COMPILER2
      // C2 uses ebp as a general register see if NULL fp helps
      frame ret_frame2(ret_sp, NULL, addr.pc());
      if (!ret_frame2.safe_for_sender(jt)) {
        // nothing else to try if the frame isn't good
        return false;
      }
      ret_frame = ret_frame2;
#else
      // nothing else to try if the frame isn't good
      return false;
#endif /* COMPILER2 */
    }
    *fr_addr = ret_frame;
    return true;
  }

  // nothing else to try
  return false;
}

void JavaThread::cache_global_variables() { }

static char* get_java_executable_path() {
  const char* java_home = Arguments::get_property("java.home");
  if (java_home != NULL) {
    char* path = NEW_C_HEAP_ARRAY(char, MAXPATHLEN, mtInternal);
    jio_snprintf(path, MAXPATHLEN, "%s/bin/java", java_home);
    return path;
  }
  return os::strdup("java");
}

static bool can_read_classlist(const char* class_list_path) {
  int fd = open(class_list_path, O_RDONLY);
  if (fd >= 0) {
    if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
      return true;
    }
  }
  return false;
}

static char* copy_identity(const char* value, size_t length) {
  char* result = NEW_C_HEAP_ARRAY(char, length + 1, mtInternal);
  memcpy(result, value, length);
  result[length] = '\0';
  return result;
}

static char* get_effective_user_identity() {
  uid_t effective_uid = geteuid();
  long configured_size = sysconf(_SC_GETPW_R_SIZE_MAX);
  size_t buffer_size = configured_size > 0 ? (size_t)configured_size
                                           : (size_t)1024;
  const size_t max_buffer_size = 1024 * 1024;
  buffer_size = MIN2(buffer_size, max_buffer_size);

  char* buffer = NEW_C_HEAP_ARRAY(char, buffer_size, mtInternal);
  struct passwd entry;
  struct passwd* result = NULL;
  int status = 0;
  while (true) {
    status = getpwuid_r(effective_uid, &entry, buffer, buffer_size, &result);
    if (status != ERANGE || buffer_size == max_buffer_size) {
      break;
    }
    FREE_C_HEAP_ARRAY(char, buffer, mtInternal);
    buffer_size = MIN2(buffer_size * 2, max_buffer_size);
    buffer = NEW_C_HEAP_ARRAY(char, buffer_size, mtInternal);
  }

  char* identity = NULL;
  if (status == 0 && result != NULL && result->pw_name != NULL &&
      result->pw_name[0] != '\0') {
    identity = copy_identity(result->pw_name, strlen(result->pw_name));
  } else {
    char fallback[64];
    jio_snprintf(fallback, sizeof(fallback), "uid-%lu",
                 (unsigned long)effective_uid);
    identity = copy_identity(fallback, strlen(fallback));
    if (PrintAutoAppCDS) {
      warning("Could not resolve effective user name; using %s.", identity);
    }
  }

  FREE_C_HEAP_ARRAY(char, buffer, mtInternal);
  return identity;
}

static char* get_application_identity() {
  const char* command = Arguments::java_command();
  if (command == NULL) {
    return copy_identity("java", sizeof("java") - 1);
  }

  while (*command != '\0' && isspace((unsigned char)*command)) {
    command++;
  }
  const char* end = command;
  while (*end != '\0' && !isspace((unsigned char)*end)) {
    end++;
  }
  return end == command
         ? copy_identity("java", sizeof("java") - 1)
         : copy_identity(command, (size_t)(end - command));
}

static char* normalize_identity(const char* identity) {
  size_t length = strlen(identity);
  char* normalized = NEW_C_HEAP_ARRAY(char, length + 1, mtInternal);
  for (size_t i = 0; i < length; i++) {
    unsigned char value = (unsigned char)identity[i];
    normalized[i] = value == '/' || value <= 0x1f || value == 0x7f
                    ? '_' : identity[i];
  }
  normalized[length] = '\0';
  return normalized;
}

static bool construct_identity_path(char* destination,
                                    size_t destination_size,
                                    const char* base,
                                    const char* file_name) {
  size_t required = strlen(base) + 1 + strlen(file_name);
  if (required >= destination_size) {
    return false;
  }
  jio_snprintf(destination, destination_size, "%s/%s", base, file_name);
  return true;
}

static uint64_t update_identity_hash(uint64_t hash, const char* value) {
  size_t length = strlen(value);
  for (size_t i = 0; i < sizeof(length); i++) {
    hash ^= (unsigned char)((length >> (i * 8)) & 0xff);
    hash *= UCONST64(0x100000001b3);
  }
  for (size_t i = 0; i < length; i++) {
    hash ^= (unsigned char)value[i];
    hash *= UCONST64(0x100000001b3);
  }
  return hash;
}

static uint64_t identity_hash(const char* user, const char* application) {
  uint64_t hash = UCONST64(0xcbf29ce484222325);
  hash = update_identity_hash(hash, user);
  return update_identity_hash(hash, application);
}

static size_t auto_appcds_name_max(const char* base_path) {
  errno = 0;
  long value = pathconf(base_path, _PC_NAME_MAX);
  size_t name_max = value > 0 ? (size_t)value : (size_t)255;
  return MIN2(name_max, (size_t)PATH_MAX - 1);
}

static bool build_identity_stem(char* destination, size_t destination_size,
                                const char* raw_user,
                                const char* raw_application,
                                const char* user,
                                const char* application,
                                size_t stem_max) {
  const size_t prefix_length = strlen("appcds_");
  const size_t user_length = strlen(user);
  const size_t application_length = strlen(application);
  const size_t readable_length = prefix_length + user_length + 1 +
                                 application_length;

  stem_max = MIN2(stem_max, destination_size - 1);
  if (readable_length <= stem_max) {
    int written = jio_snprintf(destination, destination_size,
                               "appcds_%s_%s", user, application);
    return written >= 0 && (size_t)written == readable_length;
  }

  const size_t hash_suffix_length = strlen("_h") + 16;
  const size_t fixed_length = prefix_length + 1 + hash_suffix_length;
  if (stem_max < fixed_length) {
    return false;
  }

  size_t readable_capacity = stem_max - fixed_length;
  size_t user_keep = MIN2(user_length, readable_capacity);
  size_t application_keep = readable_capacity - user_keep;
  uint64_t hash = identity_hash(raw_user, raw_application);

  int written = jio_snprintf(destination, destination_size,
                             "appcds_%.*s_%.*s_h%016" PRIx64,
                             (int)user_keep, user,
                             (int)application_keep, application,
                             hash);
  return written >= 0 && (size_t)written <= stem_max;
}

static bool append_identity_suffix(char* destination,
                                   size_t destination_size,
                                   const char* stem,
                                   const char* suffix,
                                   size_t name_max) {
  size_t required = strlen(stem) + strlen(suffix);
  if (required > name_max || required >= destination_size) {
    return false;
  }
  int written = jio_snprintf(destination, destination_size,
                             "%s%s", stem, suffix);
  return written >= 0 && (size_t)written == required;
}

static bool build_identity_paths(const char* base_path,
                                 char* class_list_path,
                                 size_t class_list_size,
                                 char* coop_path,
                                 size_t coop_size,
                                 char* nocoop_path,
                                 size_t nocoop_size) {
  char* raw_user = get_effective_user_identity();
  char* raw_application = get_application_identity();
  char* user = normalize_identity(raw_user);
  char* application = normalize_identity(raw_application);

  char stem[PATH_MAX];
  char class_list_name[PATH_MAX];
  char coop_name[PATH_MAX];
  char nocoop_name[PATH_MAX];
  size_t name_max = auto_appcds_name_max(base_path);
  const size_t longest_suffix_length = strlen("_nocoop.jsa");

  bool success = name_max > longest_suffix_length &&
      build_identity_stem(stem, sizeof(stem),
                          raw_user, raw_application,
                          user, application,
                          name_max - longest_suffix_length) &&
      append_identity_suffix(class_list_name, sizeof(class_list_name),
                             stem, ".lst", name_max) &&
      append_identity_suffix(coop_name, sizeof(coop_name),
                             stem, "_coop.jsa", name_max) &&
      append_identity_suffix(nocoop_name, sizeof(nocoop_name),
                             stem, "_nocoop.jsa", name_max) &&
      construct_identity_path(class_list_path, class_list_size,
                              base_path, class_list_name) &&
      construct_identity_path(coop_path, coop_size,
                              base_path, coop_name) &&
      construct_identity_path(nocoop_path, nocoop_size,
                              base_path, nocoop_name);

  FREE_C_HEAP_ARRAY(char, application, mtInternal);
  FREE_C_HEAP_ARRAY(char, user, mtInternal);
  FREE_C_HEAP_ARRAY(char, raw_application, mtInternal);
  FREE_C_HEAP_ARRAY(char, raw_user, mtInternal);

  if (!success) {
    warning("Auto AppCDS identity path is too long; Auto AppCDS is disabled for this VM.");
  }
  return success;
}

static void construct_path(char *dest, size_t dest_size, const char *base, const char *suffix) {
  size_t base_len = strlen(base);
  size_t suffix_len = strlen(suffix);
  guarantee(base_len + suffix_len < dest_size, "base path too long!");

  jio_snprintf(dest, dest_size, "%s%s", base, suffix);
}

static void create_jsa_with_coop_option(const char* class_list_path, const char* appcds_path,
                                        const JavaVMInitArgs* original_args, bool use_compressed_oops) {
  static const char shared_class_list_option[] = "-XX:SharedClassListFile=";
  static const char shared_archive_option[] = "-XX:SharedArchiveFile=";
  pid_t pid = fork();
  if (pid == 0) {
    // child process running on background
    setsid();
    signal(SIGHUP, SIG_IGN);
    const char* classpath = Arguments::get_appclasspath();
    if (classpath == NULL) {
      classpath = ".";
    }
    char* java_path = get_java_executable_path();
    int arg_count   = Arguments::num_jvm_args();
    char** vm_args  = Arguments::jvm_args_array();

    int total_args = arg_count + 11;
    char** args = NEW_C_HEAP_ARRAY(char*, total_args + 1, mtInternal);
    int idx = 0;

    args[idx++] = java_path;
    args[idx++] = os::strdup("-Xshare:dump");
    args[idx++] = os::strdup("-XX:+UseAppCDS");

    char shared_class_list_file[PATH_MAX + sizeof(shared_class_list_option)];
    construct_path(shared_class_list_file, sizeof(shared_class_list_file),
                   shared_class_list_option, class_list_path);
    args[idx++] = os::strdup(shared_class_list_file);

    char shared_archive_file[PATH_MAX + sizeof(shared_archive_option)];
    construct_path(shared_archive_file, sizeof(shared_archive_file),
                   shared_archive_option, appcds_path);
    args[idx++] = os::strdup(shared_archive_file);

    args[idx++] = os::strdup("-classpath");
    args[idx++] = os::strdup(classpath);

    // copy the original parameters and filter out the conflicting ones
    for (int i = 0; i < arg_count; i++) {
      if (vm_args[i] != NULL && strstr(vm_args[i], "AutoSharedArchivePath") == NULL
                             && strstr(vm_args[i], "JProfilingCacheAutoArchiveDir") == NULL
                             && strstr(vm_args[i], "UseCompressedOops") == NULL) {
        args[idx++] = os::strdup(vm_args[i]);
      }
    }

    args[idx++] = os::strdup("-Xms128m");
    args[idx++] = os::strdup("-Xmx256m");

    if (use_compressed_oops) {
      args[idx++] = os::strdup("-XX:+UseCompressedOops");
    } else {
      args[idx++] = os::strdup("-XX:-UseCompressedOops");
    }

    args[idx++] = os::strdup("-version");
    args[idx] = NULL;

    if (PrintAutoAppCDS) {
      tty->print_cr("Creating JSA with UseCompressedOops=%s", use_compressed_oops ? "true" : "false");
      int i = 0;
      while (args[i] != NULL) {
        tty->print_cr("args[%d] = %s", i, args[i]);
        i++;
      }
    }
    execv(java_path, args);
  }
}

// create missing JSA files (both coop and nocoop versions if needed)
static void try_create_missing_jsa(const char* class_list_path,
                                   const char* appcds_coop_path,
                                   const char* appcds_nocoop_path,
                                   const JavaVMInitArgs* original_args) {
  struct stat st;
  bool coop_exists = (stat(appcds_coop_path, &st) == 0);
  bool nocoop_exists = (stat(appcds_nocoop_path, &st) == 0);

  if (!coop_exists) {
    if (PrintAutoAppCDS) {
      tty->print_cr("Creating missing JSA file with compressed oops: %s", appcds_coop_path);
    }
    create_jsa_with_coop_option(class_list_path, appcds_coop_path, original_args, true);
  }

  if (!nocoop_exists) {
    if (PrintAutoAppCDS) {
      tty->print_cr("Creating missing JSA file without compressed oops: %s", appcds_nocoop_path);
    }
    create_jsa_with_coop_option(class_list_path, appcds_nocoop_path, original_args, false);
  }
}

void JavaThread::handle_appcds_for_executor(const JavaVMInitArgs* args) {
  if (FLAG_IS_DEFAULT(AutoSharedArchivePath)) {
    return;
  }

  if (AutoSharedArchivePath == NULL) {
    warning("AutoSharedArchivePath should not be empty. Please set the specific path.");
    return;
  }

  static char base_path[JVM_MAXPATHLEN] = {'\0'};
  if (UseAutoAppCDSIdentity &&
      strlen(AutoSharedArchivePath) >= sizeof(base_path)) {
    warning("AutoSharedArchivePath is too long; Auto AppCDS is disabled for this VM.");
    return;
  }
  jio_snprintf(base_path, sizeof(base_path), "%s", AutoSharedArchivePath);

  struct stat st;
  if (stat(base_path, &st) != 0) {
    if (mkdir(base_path, 0755) != 0) {
      vm_exit_during_initialization(err_msg("Can't create dirs %s : %s", base_path, strerror(errno)));
    }
  } else {
    if (!S_ISDIR(st.st_mode)) {
      vm_exit_during_initialization(err_msg("Path %s exists but is not a directory.", base_path));
    }

    if (access(base_path, R_OK | W_OK | X_OK) != 0) {
      vm_exit_during_initialization(err_msg("Insufficient permissions for directory %s. Requires read,"
                                    " write, and execute access. Error: %s", base_path, strerror(errno)));
    }
  }

  char class_list_path[PATH_MAX];
  char appcds_coop_path[PATH_MAX];
  char appcds_nocoop_path[PATH_MAX];

  if (UseAutoAppCDSIdentity) {
    if (!build_identity_paths(base_path,
                              class_list_path, sizeof(class_list_path),
                              appcds_coop_path, sizeof(appcds_coop_path),
                              appcds_nocoop_path, sizeof(appcds_nocoop_path))) {
      return;
    }
  } else {
    construct_path(class_list_path, sizeof(class_list_path),
                   base_path, "/appcds.lst");
    construct_path(appcds_coop_path, sizeof(appcds_coop_path),
                   base_path, "/appcds_coop.jsa");
    construct_path(appcds_nocoop_path, sizeof(appcds_nocoop_path),
                   base_path, "/appcds_nocoop.jsa");
  }

  const char* current_appcds_path = UseCompressedOops
                                    ? appcds_coop_path
                                    : appcds_nocoop_path;

  if (PrintAutoAppCDS) {
    tty->print_cr("classlist file : %s", class_list_path);
    tty->print_cr("appcds jsa file : %s", current_appcds_path);
    tty->print_cr("UseCompressedOops : %s", UseCompressedOops ? "true" : "false");
  }

  const char* class_list_ptr = class_list_path;
  const char* appcds_ptr = current_appcds_path;

  if (stat(current_appcds_path, &st) == 0) {
    try_create_missing_jsa(class_list_path,
                           appcds_coop_path, appcds_nocoop_path, args);

    FLAG_SET_CMDLINE(bool, UseAppCDS, true);
    FLAG_SET_CMDLINE(bool, UseSharedSpaces, true);
    FLAG_SET_CMDLINE(bool, RequireSharedSpaces, true);
    CommandLineFlags::ccstrAtPut("SharedArchiveFile", &appcds_ptr, Flag::COMMAND_LINE);
    if (PrintAutoAppCDS) {
      tty->print_cr("The process %d use AppCDS jsa.", os::current_process_id());
    }
    return;
  }

  if (stat(class_list_path, &st) == 0) {
    if (!can_read_classlist(class_list_path)) {
      if (PrintAutoAppCDS) {
        tty->print_cr("classlist is generating, can't create jsa by %d now.", os::current_process_id());
      }
      return;
    }

    // generate two versions of JSA
    if (stat(current_appcds_path, &st) != 0) {
      if (PrintAutoAppCDS) {
        tty->print_cr("generate jsa files by %d.", os::current_process_id());
      }
      try_create_missing_jsa(class_list_path,
                             appcds_coop_path, appcds_nocoop_path, args);
    }
  } else {
    can_read_classlist(class_list_path);
    if (PrintAutoAppCDS) {
      tty->print_cr("generate classlist file by %d.", os::current_process_id());
    }
    if (NUMANodesRandom != 0) {
      NUMANodesRandom = 0;
    }
    FLAG_SET_CMDLINE(bool, UseAppCDS, true);
    FLAG_SET_CMDLINE(bool, UseSharedSpaces, false);
    FLAG_SET_CMDLINE(bool, RequireSharedSpaces, false);
    CommandLineFlags::ccstrAtPut("DumpLoadedClassList", &class_list_ptr, Flag::COMMAND_LINE);
  }
}
