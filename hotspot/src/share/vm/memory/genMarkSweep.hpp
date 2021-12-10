/*
 * Copyright (c) 2001, 2010, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_VM_MEMORY_GENMARKSWEEP_HPP
#define SHARE_VM_MEMORY_GENMARKSWEEP_HPP

#include "gc_implementation/shared/markSweep.hpp"

class GenMarkSweep : public MarkSweep {
  friend class VM_MarkSweep;
  friend class G1MarkSweep;
  friend void marksweep_init();
  static GenMarkSweep* _the_gen_mark;
  static void gen_marksweep_init();
 public:
  static inline GenMarkSweep* the_gen_mark() { return _the_gen_mark; }
  void invoke_at_safepoint(int level, ReferenceProcessor* rp,
                                  bool clear_all_softrefs);

 private:

  // Mark live objects
  void mark_sweep_phase1(int level, bool clear_all_softrefs);
  // Calculate new addresses
  void mark_sweep_phase2();
  // Update pointers
  void mark_sweep_phase3(int level);
  // Move objects to new positions
  void mark_sweep_phase4();

  // Temporary data structures for traversal and storing/restoring marks
  void allocate_stacks();
  void deallocate_stacks();
};

#endif // SHARE_VM_MEMORY_GENMARKSWEEP_HPP
