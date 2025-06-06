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

#ifndef SHARED_VM_UTILITIES_SYMBOLREGEXMATCHER_HPP
#define SHARED_VM_UTILITIES_SYMBOLREGEXMATCHER_HPP

#include "memory/allocation.hpp"
#include "utilities/growableArray.hpp"

class SymbolPatternMatcher {
public:
    SymbolPatternMatcher() {  }
    SymbolPatternMatcher(const char* pattern, int length)
      : _regex_pattern(pattern),
        _pattern_length(length) {
    }

    ~SymbolPatternMatcher() {  }

    int   length() { return _pattern_length; }
    void  set_length(int value) { _pattern_length = value; }
    const char* regex_pattern() { return _regex_pattern; }
    void  set_regex_pattern(char* s) { _regex_pattern = s; }

private:
    int         _pattern_length;
    const char* _regex_pattern;
};

template <MEMFLAGS F>
class SymbolRegexMatcher : public ResourceObj {
public:
    SymbolRegexMatcher(const char* regexes);
    virtual ~SymbolRegexMatcher();
    GrowableArray<SymbolPatternMatcher>* patterns() { return _patterns; }

    bool matches(Symbol* symbol);
    bool matches(const char* s);

private:
    void add_regex_pattern(const char* src, int len);
    bool matches_wildcard_pattern(const char* wildcard_pattern, int pattern_length, const char* target_string);

    GrowableArray<SymbolPatternMatcher>*  _patterns;
};


#endif // SHARED_VM_UTILITIES_SYMBOLREGEXMATCHER_HPP