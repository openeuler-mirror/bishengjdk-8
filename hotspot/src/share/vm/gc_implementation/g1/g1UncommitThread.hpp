/*
 * Copyright (c) 2013, 2018, Red Hat, Inc. All rights reserved.
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

#ifndef SHARE_VM_GC_G1_G1CONCURRENTTHREAD_HPP
#define SHARE_VM_GC_G1_G1CONCURRENTTHREAD_HPP

#include "gc_implementation/shared/concurrentGCThread.hpp"
#include "gc_implementation/g1/g1CollectedHeap.hpp"
#include "gc_implementation/g1/heapRegionSet.hpp"

class PeriodicGC : AllStatic {
private:
  volatile static bool _should_terminate;
  static JavaThread*  _thread;
  static Monitor*     _monitor;

public:
  // Timer thread entry
  static void         timer_thread_entry(JavaThread* thread, TRAPS);
  static void         start();
  static void         stop();
  static bool         has_error(TRAPS, const char* error);
  static bool         check_for_periodic_gc();
  static bool         should_start_periodic_gc();
};

class G1UncommitThread: public ConcurrentGCThread {
  friend class VMStructs;

public:
  // Constructor
  G1UncommitThread();
  ~G1UncommitThread();

  void run();
  void stop();

  char* name() const { return (char*)"G1UncommitThread";}
};

#endif // SHARE_VM_GC_G1_G1CONCURRENTTHREAD_HPP
