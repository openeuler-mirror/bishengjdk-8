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
 */

#include "precompiled.hpp"
#include "classfile/symbolTable.hpp"
#include "compiler/compileBroker.hpp"
#include "jprofilecache/jitProfileCacheUtils.hpp"
#include "runtime/compilationPolicy.hpp"
#include "runtime/thread.hpp"

Symbol* JitProfileCacheUtils::get_class_loader_name(ClassLoaderData* cld) {
  Handle class_loader(Thread::current(), cld->class_loader());
  Thread* THREAD = Thread::current();
  Symbol* loader_name = NULL;
  if (class_loader() != NULL) {
    loader_name = JitProfileCacheUtils::remove_meaningless_suffix(class_loader()->klass()->name());
  } else {
    loader_name = SymbolTable::new_symbol("nullptr", THREAD);
  }
  return loader_name;
}

Symbol* JitProfileCacheUtils::remove_meaningless_suffix(Symbol* s) {
  ResourceMark rm;
  Thread* THREAD = Thread::current();
  Symbol* result = s;
  char* s_char = s->as_C_string();
  int len = (int)::strlen(s_char);
  int i = 0;
  for (i = 0; i < len - 1; i++) {
    if (s_char[i] == '$' && s_char[i+1] == '$') {
      break;
    }
  }
  if (i < len - 1) {
    i = (i == 0) ? 1: i;
    result = SymbolTable::new_symbol(s_char, i, THREAD);
    s_char = result->as_C_string();
  }
  len = (int)::strlen(s_char);
  i = len - 1;
  for (; i >= 0; i--) {
    if (s_char[i] >= '0' && s_char[i] <= '9') {
      continue;
    } else if (s_char[i] == '$') {
      continue;
    } else {
      break;
    }
  }
  if (i != len - 1){
    i = i == -1 ? 0 : i;
    result = SymbolTable::new_symbol(s_char, i + 1, THREAD);
  }
  return result;
}

bool JitProfileCacheUtils::commit_compilation(methodHandle m, int comp_level, int bci, TRAPS) {
  if (comp_level > JProfilingCacheMaxTierLimit) {
    comp_level = JProfilingCacheMaxTierLimit;
  }
  if (CompilationPolicy::can_be_compiled(m, comp_level)) {
      CompileBroker::compile_method(m, bci, comp_level,
                                    methodHandle(), 1,
                                    "JitProfileCache", THREAD);
      return true;
  }
  return false;
}

bool JitProfileCacheUtils::is_in_unpreloadable_classes_black_list(Symbol* s) {
  ResourceMark rm;
  const char * str = s->as_C_string();
  return is_in_unpreloadable_classes_black_list(str);
}

bool JitProfileCacheUtils::is_in_unpreloadable_classes_black_list(const char* str) {
  static const char* const JFR_PREFIX = "jdk/jfr";
  static const int JFR_PREFIX_LEN = strlen(JFR_PREFIX);
  static const char* const KRB5_PREFIX = "sun/security/krb5";
  static const int KRB5_PREFIX_LEN = strlen(KRB5_PREFIX);
  static const char* const PLATFORMLOGGER_PREFIX = "sun/util/logging/PlatformLogger";
  static const int PLATFORMLOGGER_LEN = strlen(PLATFORMLOGGER_PREFIX);
  return strncmp(str, JFR_PREFIX, JFR_PREFIX_LEN) == 0 ||
         strncmp(str, KRB5_PREFIX, KRB5_PREFIX_LEN) == 0 ||
         strncmp(str, PLATFORMLOGGER_PREFIX, PLATFORMLOGGER_LEN) == 0;
}
