/*
 * Copyright (c) 2001, 2019, Oracle and/or its affiliates. All rights reserved.
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

#include "gc_implementation/g1/g1UncommitThread.hpp"
#include "gc_implementation/g1/g1_globals.hpp"
#include "runtime/javaCalls.hpp"
#include "runtime/os.hpp"

#ifdef _WINDOWS
#pragma warning(disable : 4355)
#endif

volatile bool PeriodicGC::_should_terminate = false;
JavaThread* PeriodicGC::_thread = NULL;
Monitor*    PeriodicGC::_monitor = NULL;

bool PeriodicGC::has_error(TRAPS, const char* error) {
  if (HAS_PENDING_EXCEPTION) {
    tty->print_cr("%s", error);
    java_lang_Throwable::print(PENDING_EXCEPTION, tty);
    tty->cr();
    CLEAR_PENDING_EXCEPTION;
    return true;
  } else {
    return false;
  }
}

void PeriodicGC::start() {
  _monitor = new Monitor(Mutex::nonleaf, "PeriodicGC::_monitor", Mutex::_allow_vm_block_flag);

  EXCEPTION_MARK;
  Klass* k = SystemDictionary::resolve_or_fail(vmSymbols::java_lang_Thread(), true, CHECK);
  instanceKlassHandle klass (THREAD, k);
  instanceHandle thread_oop = klass->allocate_instance_handle(CHECK);

  const char thread_name[] = "periodic gc timer";
  Handle string = java_lang_String::create_from_str(thread_name, CHECK);

  // Initialize thread_oop to put it into the system threadGroup
  Handle thread_group (THREAD, Universe::system_thread_group());
  JavaValue result(T_VOID);
  JavaCalls::call_special(&result, thread_oop,
                       klass,
                       vmSymbols::object_initializer_name(),
                       vmSymbols::threadgroup_string_void_signature(),
                       thread_group,
                       string,
                       THREAD);
  if (has_error(THREAD, "Exception in VM (PeriodicGC::start) : ")) {
    vm_exit_during_initialization("Cannot create periodic gc timer thread.");
    return;
  }

  KlassHandle group(THREAD, SystemDictionary::ThreadGroup_klass());
  JavaCalls::call_special(&result,
                        thread_group,
                        group,
                        vmSymbols::add_method_name(),
                        vmSymbols::thread_void_signature(),
                        thread_oop,             // ARG 1
                        THREAD);
  if (has_error(THREAD, "Exception in VM (PeriodicGC::start) : ")) {
    vm_exit_during_initialization("Cannot create periodic gc timer thread.");
    return;
  }

  {
    MutexLocker mu(Threads_lock);
    _thread = new JavaThread(&PeriodicGC::timer_thread_entry);
    if (_thread == NULL || _thread->osthread() == NULL) {
      vm_exit_during_initialization("Cannot create PeriodicGC timer thread. Out of system resources.");
    }

    java_lang_Thread::set_thread(thread_oop(), _thread);
    java_lang_Thread::set_daemon(thread_oop());
    _thread->set_threadObj(thread_oop());
    Threads::add(_thread);
    Thread::start(_thread);
  }
}

void PeriodicGC::timer_thread_entry(JavaThread* thread, TRAPS) {
  while(!_should_terminate) {
    assert(!SafepointSynchronize::is_at_safepoint(), "PeriodicGC timer thread is a JavaThread");
    G1CollectedHeap::heap()->check_trigger_periodic_gc();

    MutexLockerEx x(_monitor);
    if (_should_terminate) {
      break;
    }
    _monitor->wait(false /* no_safepoint_check */, 200);
  }
}

void PeriodicGC::stop() {
  _should_terminate = true;
  {
    MutexLockerEx ml(_monitor, Mutex::_no_safepoint_check_flag);
    _monitor->notify();
  }
}

G1UncommitThread::G1UncommitThread() :
  ConcurrentGCThread() {
  if (os::create_thread(this, os::cgc_thread)) {
    int native_prio;
    if (G1UncommitThreadPriority) {
      native_prio = os::java_to_os_priority[CriticalPriority];
    } else {
      native_prio = os::java_to_os_priority[NearMaxPriority];
    }
    os::set_native_priority(this, native_prio);
    if (!_should_terminate && !DisableStartThread) {
      os::start_thread(this);
    }
  }
  if (G1UncommitLog) {
    gclog_or_tty->print_cr("Periodic GC Thread start");
  }
}

G1UncommitThread::~G1UncommitThread() {
  // This is here so that super is called.
}

void G1UncommitThread::run() {
  G1CollectedHeap* heap = G1CollectedHeap::heap();
  while (!_should_terminate) {
    heap->_hrm.free_uncommit_list_memory();
    os::sleep(this, G1PeriodicGCInterval / 10, false);
  }
  terminate();
}

void G1UncommitThread::stop() {
  {
    MutexLockerEx ml(Terminator_lock);
    _should_terminate = true;
  }
  {
    MutexLockerEx ml(Terminator_lock);
    while (!_has_terminated) {
      Terminator_lock->wait();
    }
  }
}
