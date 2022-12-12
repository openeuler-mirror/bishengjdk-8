/*
 * Copyright (c) 2022 SAP SE. All rights reserved.
 * Copyright (c) 2022, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2022, Huawei Technologies Co., Ltd. All rights reserved.
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
#include "gc_implementation/shared/concurrentGCThread.hpp"
#include "gc_implementation/shared/gcTrimNativeHeap.hpp"
#include "gc_implementation/g1/g1_globals.hpp"
#include "runtime/globals.hpp"
#include "runtime/globals_extension.hpp"
#include "runtime/mutex.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/os.hpp"
#include "utilities/debug.hpp"
#include "utilities/globalDefinitions.hpp"
#include "utilities/ostream.hpp"
#include "utilities/ticks.hpp"

bool GCTrimNative::_async_mode = false;
double GCTrimNative::_next_trim_not_before = 0;

// GCTrimNative works in two modes:
//
// - async mode, where GCTrimNative runs a trimmer thread on behalf of the GC.
//   The trimmer thread will be doing all the trims, both periodically and
//   triggered from outside via GCTrimNative::schedule_trim().
//
// - synchronous mode, where the GC does the trimming itself in its own thread,
//   via GCTrimNative::should_trim() and GCTrimNative::execute_trim().
//
// The mode is set as argument to GCTrimNative::initialize().

class NativeTrimmer : public ConcurrentGCThread {

  Monitor* _lock;
  volatile jlong _paused;
  static NativeTrimmer* _the_trimmer;

public:

  virtual void run() {
    initialize_in_thread();
    wait_for_universe_init();

    assert(GCTrimNativeHeap, "Sanity");
    assert(os::can_trim_native_heap(), "Sanity");

    gclog_or_tty->print_cr("NativeTrimmer started.");

    // Note: GCTrimNativeHeapInterval=0 -> zero wait time -> indefinite waits, disabling periodic trim
    const int64_t delay_ms = GCTrimNativeHeapInterval * 1000;
    for (;;) {
      MonitorLockerEx ml(_lock, Mutex::_no_safepoint_check_flag);
      ml.wait(Mutex::_no_safepoint_check_flag, delay_ms);
      if (_should_terminate) {
        gclog_or_tty->print_cr("NativeTrimmer stopped.");
        break;
      }
      jlong paused = Atomic::load(&_paused);
      if (!paused && os::should_trim_native_heap()) {
        GCTrimNative::do_trim();
      }
    }

    terminate();
  }

  void stop() {
    {
      MutexLockerEx ml(Terminator_lock);
      _should_terminate = true;
    }

    wakeup();

    {
      MutexLockerEx ml(Terminator_lock);
      while (!_has_terminated) {
        Terminator_lock->wait();
      }
    }
  }

protected:

  void wakeup() {
    MonitorLockerEx ml(_lock, Mutex::_no_safepoint_check_flag);
    ml.notify_all();
  }

  void pause() {
    Atomic::store(1, &_paused);
    debug_only(gclog_or_tty->print_cr("NativeTrimmer paused"));
  }

  void unpause() {
    Atomic::store(0, &_paused);
    debug_only(gclog_or_tty->print_cr("NativeTrimmer unpaused"));
  }

public:

  NativeTrimmer() :
    _paused(0)
  {
    //Mutex::leaf+8 just for NativeTrimmer_lock
    _lock = new (std::nothrow) Monitor(Mutex::leaf+8, "NativeTrimmer_lock", true);
    set_name("NativeTrimmer Thread");
  }

  static bool is_enabled() {
    return _the_trimmer != NULL;
  }

  static void start_trimmer() {
    _the_trimmer = new NativeTrimmer();
    _the_trimmer->create_and_start(NormPriority);
  }

  static void stop_trimmer() {
    _the_trimmer->stop();
  }

  static void pause_periodic_trim() {
    _the_trimmer->pause();
  }

  static void unpause_periodic_trim() {
    _the_trimmer->unpause();
  }

  static void schedule_trim_now() {
    _the_trimmer->unpause();
    _the_trimmer->wakeup();
  }

}; // NativeTrimmer

NativeTrimmer* NativeTrimmer::_the_trimmer = NULL;

void GCTrimNative::do_trim() {
  Ticks start = Ticks::now();
  os::size_change_t sc;
  if (os::trim_native_heap(&sc)) {
    Tickspan trim_time = (Ticks::now() - start);
    if (sc.after != SIZE_MAX) {
      const size_t delta = sc.after < sc.before ? (sc.before - sc.after) : (sc.after - sc.before);
      const char sign = sc.after < sc.before ? '-' : '+';
      gclog_or_tty->print_cr("Trim native heap: RSS+Swap: " PROPERFMT "->" PROPERFMT " (%c" PROPERFMT "), %1.3fms",
                         PROPERFMTARGS(sc.before), PROPERFMTARGS(sc.after), sign, PROPERFMTARGS(delta),
                         trim_time.seconds() * 1000);
    } else {
      gclog_or_tty->print_cr("Trim native heap (no details)");
    }
  }
}

/// GCTrimNative outside facing methods

void GCTrimNative::initialize(bool async_mode) {

  if (GCTrimNativeHeap) {

    if (!os::can_trim_native_heap()) {
      FLAG_SET_ERGO(bool, GCTrimNativeHeap, false);
      gclog_or_tty->print_cr("GCTrimNativeHeap disabled - trim-native not supported on this platform.");
      return;
    }

    debug_only(gclog_or_tty->print_cr("GCTrimNativeHeap enabled."));

    _async_mode = async_mode;

    // If we are to run the trimmer on behalf of the GC:
    if (_async_mode) {
      NativeTrimmer::start_trimmer();
    }

    _next_trim_not_before = GCTrimNativeHeapInterval;
  }
}

void GCTrimNative::cleanup() {
  if (GCTrimNativeHeap) {
    if (_async_mode) {
      NativeTrimmer::stop_trimmer();
    }
  }
}

bool GCTrimNative::should_trim(bool ignore_delay) {
  return
      GCTrimNativeHeap && os::can_trim_native_heap() &&
      (ignore_delay || (GCTrimNativeHeapInterval > 0 && os::elapsedTime() > _next_trim_not_before)) &&
      os::should_trim_native_heap();
}

void GCTrimNative::execute_trim() {
  if (GCTrimNativeHeap) {
    assert(!_async_mode, "Only call for non-async mode");
    do_trim();
    _next_trim_not_before = os::elapsedTime() + GCTrimNativeHeapInterval;
  }
}

void GCTrimNative::pause_periodic_trim() {
  if (GCTrimNativeHeap) {
    assert(_async_mode, "Only call for async mode");
    NativeTrimmer::pause_periodic_trim();
  }
}

void GCTrimNative::unpause_periodic_trim() {
  if (GCTrimNativeHeap) {
    assert(_async_mode, "Only call for async mode");
    NativeTrimmer::unpause_periodic_trim();
  }
}

void GCTrimNative::schedule_trim() {
  if (GCTrimNativeHeap) {
    assert(_async_mode, "Only call for async mode");
    NativeTrimmer::schedule_trim_now();
  }
}
