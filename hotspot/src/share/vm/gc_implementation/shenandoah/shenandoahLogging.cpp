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

#include <stdio.h> // for va_list and friends
#include "gc_implementation/shenandoah/shenandoahLogging.hpp"
#include "utilities/ostream.hpp"

void ShenandoahLogger::handle_warning(const char* format, ...) {
  va_list ap;
  va_start(ap, format);
  handle_generic(format, ap);
  va_end(ap);
}

void ShenandoahLogger::handle_trace(const char* format, ...) {
  va_list ap;
  va_start(ap, format);
  handle_generic(format, ap);
  va_end(ap);
}

void ShenandoahLogger::handle_debug(const char* format, ...) {
  va_list ap;
  va_start(ap, format);
  handle_generic(format, ap);
  va_end(ap);
}

void ShenandoahLogger::handle_info(const char* format, ...) {
  va_list ap;
  va_start(ap, format);
  handle_generic(format, ap);
  va_end(ap);
}

void ShenandoahLogger::handle_generic(const char* format, va_list ap) {
  gclog_or_tty->bol();
  gclog_or_tty->sp(gclog_or_tty->indentation()*4);
  gclog_or_tty->vprint_cr(format, ap);
}
