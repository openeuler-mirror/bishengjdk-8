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

#include <sys/file.h>

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

static char* get_complete_classpath() {
  const char* env_cp = Arguments::get_property("env.class.path");
  if (env_cp == NULL || env_cp[0] == '\0') {
    env_cp = Arguments::get_property("java.class.path");
  }
  return (char *)env_cp;
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

static void construct_path(char *dest, size_t dest_size, const char *base, const char *suffix) {
  size_t base_len = strlen(base);
  size_t suffix_len = strlen(suffix);
  guarantee(base_len + suffix_len < dest_size, "base path too long!");

  jio_snprintf(dest, dest_size, "%s%s", base, suffix);
}

static void create_jsa(const char* class_list_path, const char* appcds_path, const JavaVMInitArgs* original_args) {
  pid_t pid = fork();
  if (pid == 0) {
    // child process running on background
    setsid();
    signal(SIGHUP, SIG_IGN);
    const char* classpath = get_complete_classpath();
    if (classpath == NULL) {
      classpath = ".";
    }
    char* java_path = get_java_executable_path();
    int arg_count   = Arguments::num_jvm_args();
    char** vm_args  = Arguments::jvm_args_array();

    int total_args = arg_count + 8;
    char** args = NEW_C_HEAP_ARRAY(char*, total_args + 1, mtInternal);
    int idx = 0;

    args[idx++] = java_path;
    args[idx++] = os::strdup("-Xshare:dump");
    args[idx++] = os::strdup("-XX:+UseAppCDS");

    char shared_class_list_file[PATH_MAX];
    char shared_archive_file[PATH_MAX];
    construct_path(shared_class_list_file, sizeof(shared_class_list_file), "-XX:SharedClassListFile=", class_list_path);
    construct_path(shared_archive_file, sizeof(shared_archive_file), "-XX:SharedArchiveFile=", appcds_path);

    args[idx++] = strdup(shared_class_list_file);
    args[idx++] = strdup(shared_archive_file);

    args[idx++] = os::strdup("-classpath");
    args[idx++] = os::strdup(classpath);
    for (int i = 0; i < arg_count; i++) {
      if (vm_args[i] != NULL) {
        args[idx++] = os::strdup(vm_args[i]);
      }
    }
    args[idx++] = os::strdup("-version");
    args[idx] = NULL;

    if (PrintAutoAppCDS) {
      int i = 0;
      while (args[i] != NULL) {
        tty->print_cr("args[%d] = %s", i, args[i]);
        i++;
      }
    }
    execv(java_path, args);
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
  jio_snprintf(base_path, sizeof(base_path), "%s", AutoSharedArchivePath);

  struct stat st;
  if (stat(base_path, &st) != 0) {
    if (mkdir(base_path, 0755) != 0) {
      vm_exit_during_initialization(err_msg("can't create dirs %s : %s", base_path, strerror(errno)));
    }
  }

  char class_list_path[PATH_MAX];
  char appcds_path[PATH_MAX];

  construct_path(class_list_path, sizeof(class_list_path), base_path, "/appcds.lst");
  construct_path(appcds_path, sizeof(appcds_path), base_path, "/appcds.jsa");

  if (PrintAutoAppCDS) {
    tty->print_cr("classlist file : %s", class_list_path);
    tty->print_cr("jsa file : %s", appcds_path);
  }

  const char* class_list_ptr = class_list_path;
  const char* appcds_ptr = appcds_path;

  if (stat(appcds_path, &st) == 0) {
    FLAG_SET_CMDLINE(bool, UseAppCDS, true);
    FLAG_SET_CMDLINE(bool, UseSharedSpaces, true);
    FLAG_SET_CMDLINE(bool, RequireSharedSpaces, true);
    CommandLineFlags::ccstrAtPut("SharedArchiveFile", &appcds_ptr, Flag::COMMAND_LINE);
    if (PrintAutoAppCDS) {
      tty->print_cr("Use AppCDS JSA.");
    }
    return;
  }

  if (stat(class_list_path, &st) == 0) {
    if (!can_read_classlist(class_list_path)) {
      if(PrintAutoAppCDS) {
        tty->print_cr("classlist is generating.");
      }
      return;
    }
    if (stat(appcds_path, &st) != 0) {
      if (PrintAutoAppCDS) {
        tty->print_cr("Create JSA file.");
      }
      create_jsa(class_list_path, appcds_path, args);
    }
  } else {
    can_read_classlist(class_list_path);
    FLAG_SET_CMDLINE(bool, UseAppCDS, true);
    FLAG_SET_CMDLINE(bool, UseSharedSpaces, false);
    FLAG_SET_CMDLINE(bool, RequireSharedSpaces, false);
    CommandLineFlags::ccstrAtPut("DumpLoadedClassList", &class_list_ptr, Flag::COMMAND_LINE);
  }
}

