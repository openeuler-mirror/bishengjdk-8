/*
 * Copyright (c) 2023, Huawei Technologies Co., Ltd. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Huawei designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Huawei in the LICENSE file that accompanied this code.
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
 * Please visit https://gitee.com/openeuler/bishengjdk-8 if you need additional
 * information or have any questions.
 */

#ifndef LINUX_X86_64_NORMAL_SERVER_SLOWDEBUG_HEAPREDACTOR_HPP
#define LINUX_X86_64_NORMAL_SERVER_SLOWDEBUG_HEAPREDACTOR_HPP
#include "../memory/allocation.hpp"
#include "oops/annotations.hpp"
#include "oops/constantPool.hpp"
#ifdef LINUX
#include "os_linux.hpp"
#endif

#define MAX_MAP_FILE_LENGTH 1024

enum HeapDumpRedactLevel {
    REDACT_UNKNOWN,
    REDACT_OFF,
    REDACT_NAMES,
    REDACT_BASIC,
    REDACT_DIYRULES,
    REDACT_ANNOTATION,
    REDACT_FULL
};

struct RedactParams {
    char* params_string;
    char* heap_dump_redact;
    char* redact_map;
    char* redact_map_file;
    char* annotation_class_path;
    char* redact_password;
};

class HeapRedactor : public StackObj {
private:
    HeapDumpRedactLevel _redact_level;
    RedactParams _redact_params;
    bool _use_sys_params;
    void* _redact_name_table;
    void* _redact_rules_table;
    void* _replace_value_table;
    void* _redact_class_field_table;
    char* _file_name_map_list;
    char* _name_map_list;
    char* _annotation_class_path;
    char* _redact_class_full_name;
    void* _redact_record;
    HeapDumpRedactLevel init_heapdump_redact_level();
    void read_redact_map_from_file(const char* path);
    void read_redact_map_dependon_mode(char* name_map_list, HeapDumpRedactLevel redact_level);
    void parse_redact_map_string(char* name_map_list);
    void parse_redact_diy_rules(char* name_map_list);
    void parse_token(char* token);
    void parse_redact_params(const char *redact_params_string);
    char* parse_redact_child_param(char *redact_params_sub_string, const char* redact_param_prefix, const char* next_redact_param_prefix);
    void init(outputStream* out);
    void init_fields();
    void init_redact_map();
    void init_class_path();

public:
    static const char* REDACT_UNKNOWN_STR;
    static const char* REDACT_OFF_STR;
    static const char* REDACT_NAMES_STR;
    static const char* REDACT_BASIC_STR;
    static const char* REDACT_DIYRULES_STR;
    static const char* REDACT_ANNOTATION_STR;
    static const char* REDACT_FULL_STR;
    HeapRedactor(outputStream* out);
    HeapRedactor(const char* redact_params, outputStream* out);
    ~HeapRedactor();
    static bool check_launcher_heapdump_redact_support(const char* value);
    HeapDumpRedactLevel redact_level() {
        if(_redact_level == REDACT_UNKNOWN) {
            _redact_level = init_heapdump_redact_level();
        }
        return _redact_level;
    }

    const char* get_redact_level_string() {
#ifdef LINUX
        switch (_redact_level) {
            case REDACT_OFF:
                return REDACT_OFF_STR;
            case REDACT_NAMES:
                return REDACT_NAMES_STR;
            case REDACT_BASIC:
                return REDACT_BASIC_STR;
            case REDACT_DIYRULES:
                return REDACT_DIYRULES_STR;
            case REDACT_ANNOTATION:
                return REDACT_ANNOTATION_STR;
            case REDACT_FULL:
                return REDACT_FULL_STR;
            case REDACT_UNKNOWN:
            default:
                return REDACT_UNKNOWN_STR;
        }
#else
        switch (_redact_level) {
            case REDACT_OFF:
                return REDACT_OFF_STR;
            case REDACT_BASIC:
                return REDACT_BASIC_STR;
            case REDACT_UNKNOWN:
            default:
                return REDACT_UNKNOWN_STR;
        }
#endif
    }

    char* lookup_redact_name(const void* name) const {
        void* val = NULL;
#ifdef LINUX
        val = os::Linux::heap_dict_lookup(const_cast<void*>(name), _redact_name_table, false);
#endif
        if(val != NULL) {
            return (char*)val;
        }
        return NULL;
    }

    void* lookup_class_rules(const void* name) {
        void* val = NULL;
#ifdef LINUX
        val = os::Linux::heap_dict_lookup(const_cast<void*>(name), _redact_rules_table, false);
#endif
        return val;
    }

    void insert_class_field_value(void* class_key, void* field_key, void* value);

    void* lookup_class_value(void* key) {
        void* val = NULL;
#ifdef LINUX
        val = os::Linux::heap_dict_lookup(key, _redact_class_field_table, false);
#endif
        return val;
    }

    const char* get_annotation_class_path(){
        return _annotation_class_path;
    }

    void insert_anonymous_value(void* key, void* value);

    template<typename T>
    T lookup_replace_value(void* key) {
        void* val = NULL;
#ifdef LINUX
        val = os::Linux::heap_dict_lookup(key, _replace_value_table, true);
#endif
        if(val != NULL) {
            return (T)val;
        }
        return NULL;
    }

    void* lookup_value(void* key, void* heap_dict, bool deletable) {
        void* val = NULL;
#ifdef LINUX
        val = os::Linux::heap_dict_lookup(key, heap_dict, deletable);
#endif
        return val;
    }

    bool lookup_annotation_index_in_constant_pool(AnnotationArray* field_annotations, ConstantPool *cp, int &byte_i_ref);
    bool recursion_cp_refs_in_element_value(AnnotationArray* field_annotations, int &byte_i_ref);
    bool recursion_cp_refs_in_annotation_struct(AnnotationArray* field_annotations, int &byte_i_ref);

    bool record_typeArrayOop(typeArrayOop array);
    void* get_vector_node_next(void* node, int &_cnt, void** &_items) {
        void* val = NULL;
#ifdef LINUX
        val = os::Linux::heap_vector_get_next(_redact_record, node, _cnt, _items);
#endif
        return val;
    }
};
#endif // LINUX_X86_64_NORMAL_SERVER_SLOWDEBUG_HEAPREDACTOR_HPP
