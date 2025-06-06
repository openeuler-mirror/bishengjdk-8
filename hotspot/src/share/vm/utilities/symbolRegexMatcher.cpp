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
 *
 */

#include "precompiled.hpp"
#include "memory/resourceArea.hpp"
#include "oops/symbol.hpp"
#include "utilities/globalDefinitions.hpp"
#include "utilities/symbolRegexMatcher.hpp"

#define SYMBOLREGEXMATCHER_INIT_SIZE 4

template <MEMFLAGS F> SymbolRegexMatcher<F>::SymbolRegexMatcher(const char* regexes)
  : _patterns(new (ResourceObj::C_HEAP, F)
                   GrowableArray<SymbolPatternMatcher>(SYMBOLREGEXMATCHER_INIT_SIZE, true, F)) {
  assert(regexes != NULL, "illegal regexes");
  int input_length = (int)strlen(regexes);
  int current_pattern_length = 0;
  char* current_pattern_start = (char*)&regexes[0];
  for (int i = 0; i < input_length + 1; i++) {
    if (regexes[i] == ',' || regexes[i] == ';' || i == input_length) {
      add_regex_pattern(current_pattern_start, current_pattern_length);
      // reset
      current_pattern_length = -1;
      current_pattern_start = (char*)&regexes[i+1];
    }
    current_pattern_length++;
  }
}

template <MEMFLAGS F> SymbolRegexMatcher<F>::~SymbolRegexMatcher() {
  delete _patterns;
}

template <MEMFLAGS F> void SymbolRegexMatcher<F>::add_regex_pattern(const char* s, int len) {
  if (len == 0) {
    return;
  }
  _patterns->push(SymbolPatternMatcher(s, len));
}

template <MEMFLAGS F> bool SymbolRegexMatcher<F>::matches(Symbol* symbol) {
  ResourceMark rm;
  char* s = symbol->as_C_string();
  return matches(s);
}

template <MEMFLAGS F> bool SymbolRegexMatcher<F>::matches(const char* s) {
  int regex_num = _patterns->length();
  for (int i = 0; i < regex_num; i++) {
    const char* regex = _patterns->at(i).regex_pattern();
    int regex_len = _patterns->at(i).length();
    if (matches_wildcard_pattern(regex, regex_len, s)) {
      return true;
    }
  }
  return false;
}

template <MEMFLAGS F> bool SymbolRegexMatcher<F>::matches_wildcard_pattern(const char* wildcard_pattern, int pattern_length, const char* target_string) {
  int s_len = (int)strlen(target_string);
  if (s_len < pattern_length - 1) {
    return false;
  }
  for (int i =0; i < pattern_length; i++) {
    if (wildcard_pattern[i] == '*') {
     return true;
    }
    if (wildcard_pattern[i] == target_string[i]) {
      continue;
    }
    if ((wildcard_pattern[i] == '.' && target_string[i] == '/')
     || (wildcard_pattern[i] == '/' && target_string[i] == '.')) {
      continue;
    }
    if (wildcard_pattern[i] != '*' && wildcard_pattern[i] != target_string[i]) {
      return false;
    }
  }
  return (s_len == pattern_length);
}

template class SymbolRegexMatcher<mtClass>;