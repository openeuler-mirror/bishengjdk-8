/*
 * Copyright (c) 2019, Red Hat, Inc. All rights reserved.
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

#include "gc_implementation/shenandoah/shenandoahSynchronizerIterator.hpp"
#include "runtime/atomic.hpp"
#include "runtime/thread.hpp"
#include "runtime/objectMonitor.hpp"
#include "runtime/objectMonitor.inline.hpp"
#include "runtime/synchronizer.hpp"
#include "runtime/safepoint.hpp"

#define CHAINMARKER (cast_to_oop<intptr_t>(-1))

// ParallelObjectSynchronizerIterator implementation
ShenandoahSynchronizerIterator::ShenandoahSynchronizerIterator()
  : _cur(ObjectSynchronizer::gBlockList) {
  assert(SafepointSynchronize::is_at_safepoint(), "Must at safepoint");
}

// Get the next block in the block list.
static inline ObjectMonitor* next(ObjectMonitor* block) {
  assert(block->object() == CHAINMARKER, "must be a block header");
  block = block->FreeNext ;
  assert(block == NULL || block->object() == CHAINMARKER, "must be a block header");
  return block;
}

ObjectMonitor* ShenandoahSynchronizerIterator::claim() {
  ObjectMonitor* my_cur = _cur;

  while (true) {
    if (my_cur == NULL) return NULL;
    ObjectMonitor* next_block = next(my_cur);
    ObjectMonitor* cas_result = (ObjectMonitor*) Atomic::cmpxchg_ptr(next_block, &_cur, my_cur);
    if (my_cur == cas_result) {
      // We succeeded.
      return my_cur;
    } else {
      // We failed. Retry with offending CAS result.
      my_cur = cas_result;
    }
  }
}

bool ShenandoahSynchronizerIterator::parallel_oops_do(OopClosure* f) {
  ObjectMonitor* block = claim();
  if (block != NULL) {
    for (int i = 1; i < ObjectSynchronizer::_BLOCKSIZE; i++) {
      ObjectMonitor* mid = &block[i];
      if (mid->object() != NULL) {
        f->do_oop((oop*) mid->object_addr());
      }
    }
    return true;
  }
  return false;
}
