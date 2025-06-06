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

#include "code/codeCache.hpp"
#include "jprofilecache/jitProfileCache.hpp"
#include "jprofilecache/jitProfileCacheThread.hpp"
#include "runtime/java.hpp"
#include "runtime/mutex.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/orderAccess.hpp"

JitProfileCacheThread* JitProfileCacheThread::_jprofilecache_thread = NULL;

JitProfileCacheThread::JitProfileCacheThread(unsigned int sec) : NamedThread() {
  set_name("JitProfileCache Flush Thread");
  set_interval_seconds(sec);
  if (os::create_thread(this, os::vm_thread)) {
      os::set_priority(this, MaxPriority);
  } else {
      tty->print_cr("[JitProfileCache] ERROR : failed to create JitProfileCacheThread");
      vm_exit(-1);
  }
}

JitProfileCacheThread::~JitProfileCacheThread() {
  // do nothing
}

#define MILLISECONDS_PER_SECOND    1000

void JitProfileCacheThread::run() {
  assert(_jprofilecache_thread == this, "sanity check");
  this->record_stack_base_and_size();
  this->_is_active = true;
  os::sleep(this, MILLISECONDS_PER_SECOND * interval_seconds(), false);
  JitProfileCache::instance()->flush_recorder();
  {
      MutexLockerEx mu(JitProfileCachePrint_lock);
      _jprofilecache_thread = NULL;
  }
}

void JitProfileCacheThread::launch_with_delay(unsigned int sec) {
  JitProfileCacheThread* t = new JitProfileCacheThread(sec);
  _jprofilecache_thread = t;
  Thread::start(t);
}

void JitProfileCacheThread::print_jit_profile_cache_thread_info_on(outputStream* st) {
  MutexLockerEx mu(JitProfileCachePrint_lock);
  if (_jprofilecache_thread == NULL || !_jprofilecache_thread->is_active()) {
      return;
  }
  st->print("\"%s\" ", _jprofilecache_thread->name());
  _jprofilecache_thread->print_on(st);
  st->cr();
}
