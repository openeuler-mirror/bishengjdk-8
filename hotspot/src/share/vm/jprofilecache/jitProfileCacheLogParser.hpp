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

#ifndef LINUX_AARCH64_NORMAL_SERVER_FASTDEBUG_JITPROFILECACHELOGPARSER_H
#define LINUX_AARCH64_NORMAL_SERVER_FASTDEBUG_JITPROFILECACHELOGPARSER_H

#include "memory/allocation.hpp"

// JitProfileCache log parser
class JitProfileCacheLogParser : CHeapObj<mtInternal> {
public:
    JitProfileCacheLogParser(randomAccessFileStream* fs, JitProfileCacheInfo* holder);
    virtual ~JitProfileCacheLogParser();

    bool valid();

    bool parse_header();
    Symbol* create_symbol(const char* char_name);
    bool parse_class();

    void check_class(int i, Symbol* name, Symbol* loader_name, Symbol* path, ProfileCacheClassChain* chain);

    bool should_ignore_this_class(Symbol* symbol);

    bool has_next_method_record();
    ProfileCacheMethodHold* parse_method();

    void increment_parsed_number_count() { _parsed_method_count++; }

    int parsed_methods() { return _parsed_method_count; }
    int total_recorder_method()  { return _total_recorder_method; }

    long file_size()              { return _file_size; }
    void set_file_size(long size) { _file_size = size; }

    int max_symbol_length() { return _max_symbol_length; }

    JitProfileCacheInfo* info_holder() { return _holder; }
    void set_info_holder(JitProfileCacheInfo* holder) { _holder = holder; }
    bool logparse_illegal_check(const char* s, bool ret_value, int end_position);
    bool logparse_illegal_count_check(int cnt, bool ret_value, int end_position);

private:
    // disable default constructor
    JitProfileCacheLogParser();

    bool                    _is_valid;
    bool                    _has_parsed_header;
    long                    _file_size;
    int                     _position;
    int                     _parsed_method_count;
    int                     _total_recorder_method;
    randomAccessFileStream* _file_stream;

    int                     _max_symbol_length;
    char*                   _parse_str_buf;

    JitProfileCacheInfo*         _holder;
    Arena*                  _arena;

    u1                      read_u1();
    u4                      read_u4();
    u8                      read_u8();
    const char*             read_string();
};


#endif // LINUX_AARCH64_NORMAL_SERVER_FASTDEBUG_JITPROFILECACHELOGPARSER_H