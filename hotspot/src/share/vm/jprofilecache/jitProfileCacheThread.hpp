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

#ifndef SHARE_VM_JPROFILECACHE_JITPROFILECACHETHREAD_HPP
#define SHARE_VM_JPROFILECACHE_JITPROFILECACHETHREAD_HPP

#include "runtime/thread.hpp"

// Thread to trigger the load of class data and profile info on delay
class JitProfileCacheThread : public AllStatic {
public:

  static unsigned int interval_seconds()   { return _interval_seconds; }

  static void  set_interval_seconds(unsigned int sec) { _interval_seconds = sec; }

  static bool  is_active() { return _is_active; }

  static void  launch_with_delay(unsigned int sec, TRAPS);

  static void  load_class_thread_entry(JavaThread* thread, TRAPS);

  static void  print_jit_profile_cache_thread_info_on(outputStream* st);

private:
  static void run(TRAPS);

  static unsigned int    _interval_seconds;
  static volatile bool   _is_active;

  static JavaThread* _jprofilecache_thread;
};

#endif //SHARE_VM_JPROFILECACHE_JITPROFILECACHETHREAD_HPP
