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
#include "classfile/vmSymbols.hpp"
#include "classfile/javaClasses.hpp"
#include "classfile/systemDictionary.hpp"
#include "code/codeCache.hpp"
#include "jprofilecache/jitProfileCache.hpp"
#include "jprofilecache/jitProfileCacheLog.hpp"
#include "jprofilecache/jitProfileCacheThread.hpp"
#include "oops/instanceKlass.hpp"
#include "runtime/handles.inline.hpp"
#include "runtime/java.hpp"
#include "runtime/javaCalls.hpp"
#include "runtime/mutex.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/orderAccess.hpp"
#include "runtime/thread.hpp"
#include "utilities/exceptions.hpp"
#include "memory/universe.hpp"

JavaThread* JitProfileCacheThread::_jprofilecache_thread = NULL;
unsigned int     JitProfileCacheThread::_interval_seconds = 0;
volatile bool    JitProfileCacheThread::_is_active = false;

bool has_error(TRAPS, const char* error) {
  if (HAS_PENDING_EXCEPTION) {
    JitProfileCacheLog::print(LogLevel::Error, "%s", error);
    java_lang_Throwable::print(PENDING_EXCEPTION, tty);
    tty->cr();
    CLEAR_PENDING_EXCEPTION;
    return true;
  } else {
    return false;
  }
}

void JitProfileCacheThread::run(TRAPS) {
  InstanceKlass* ik = InstanceKlass::cast(SystemDictionary::Thread_klass());
  assert(ik->is_initialized(), "must be initialized");
  instanceHandle thread_oop = ik->allocate_instance_handle(CHECK);
  const char thread_name[] = "delay load class/profilecache";
  Handle string = java_lang_String::create_from_str(thread_name, CHECK);
  // Initialize thread_oop to put it into the system threadGroup
  Handle thread_group(THREAD, Universe::system_thread_group());
  JavaValue result(T_VOID);

  JavaCalls::call_special(&result, thread_oop,
                          ik,
                          vmSymbols::object_initializer_name(),
                          vmSymbols::threadgroup_string_void_signature(),
                          thread_group,
                          string,
                          THREAD);
  if (has_error(THREAD, "Exception in VM (JitProfileCacheThread::run): ")) {
    vm_exit_during_initialization("Cannot create delay load class/profilecache thread.");
    return;
  }
  {
    MutexLocker mu(Threads_lock);
    JavaThread* _thread = new JavaThread(&JitProfileCacheThread::load_class_thread_entry);
    if (_thread == NULL || _thread->osthread() == NULL) {
      vm_exit_during_initialization("Cannot create PeriodicGC timer thread. Out of system resources.");
    }
    java_lang_Thread::set_thread(thread_oop(), _thread);
    // java_lang_Thread::set_daemon(thread_oop());
    _thread->set_threadObj(thread_oop());
    Threads::add(_thread);
    Thread::start(_thread);
    _is_active = true;
  }

  {
    MutexLocker mu(JitProfileCachePrint_lock);
    _jprofilecache_thread = NULL;
  }
}

void JitProfileCacheThread::load_class_thread_entry(JavaThread* thread, TRAPS) {
  os::sleep(Thread::current(), (jlong)interval_seconds(), false);
  JitProfileCache::instance()->preloader()->notify_precompilation();
}

void JitProfileCacheThread::launch_with_delay(unsigned int sec, TRAPS) {
  set_interval_seconds(sec);
  run(THREAD);
}

void JitProfileCacheThread::print_jit_profile_cache_thread_info_on(outputStream* st) {
  MutexLocker mu(JitProfileCachePrint_lock);
  if (_jprofilecache_thread == NULL || !is_active()) {
      return;
  }
  st->print("\"%s\" ", _jprofilecache_thread->name());
  _jprofilecache_thread->print_on(st);
  st->cr();
}
