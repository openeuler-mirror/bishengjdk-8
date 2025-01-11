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

#include "../runtime/globals.hpp"
#include "../runtime/os.hpp"
#include "../utilities/ostream.hpp"
#include "../memory/allocation.hpp"
#include "../memory/allocation.inline.hpp"
#include "../../../os/linux/vm/jvm_linux.h"
#include "../utilities/debug.hpp"
#include "heapRedactor.hpp"
#include "../utilities/debug.hpp"
#ifdef TARGET_ARCH_x86
# include "bytes_x86.hpp"
#endif
#ifdef TARGET_ARCH_aarch64
# include "bytes_aarch64.hpp"
#endif
#ifdef TARGET_ARCH_sparc
# include "bytes_sparc.hpp"
#endif
#ifdef TARGET_ARCH_zero
# include "bytes_zero.hpp"
#endif
#ifdef TARGET_ARCH_arm
# include "bytes_arm.hpp"
#endif
#ifdef TARGET_ARCH_ppc
# include "bytes_ppc.hpp"
#endif

const char* HeapRedactor::REDACT_UNKNOWN_STR = "UNKNOWN";
const char* HeapRedactor::REDACT_OFF_STR = "OFF";
const char* HeapRedactor::REDACT_NAMES_STR = "NAMES";
const char* HeapRedactor::REDACT_BASIC_STR = "BASIC";
const char* HeapRedactor::REDACT_DIYRULES_STR = "DIYRULES";
const char* HeapRedactor::REDACT_ANNOTATION_STR = "ANNOTATION";
const char* HeapRedactor::REDACT_FULL_STR = "FULL";

HeapRedactor::HeapRedactor(outputStream* out) {
    init_fields();
    _use_sys_params = true;
    init(out);
}

HeapRedactor::HeapRedactor(const char *redact_params_string, outputStream* out) {
    init_fields();
    if (redact_params_string != NULL && strlen(redact_params_string) > 0) {
        _use_sys_params = false;
        parse_redact_params(redact_params_string);
    } else {
        _use_sys_params = true;
    }
    init(out);
}

HeapRedactor::~HeapRedactor() {
#ifdef LINUX
    if (_redact_name_table != NULL) {
        os::Linux::heap_dict_free(_redact_name_table,false);
        _redact_name_table = NULL;
    }
    if (_redact_rules_table != NULL) {
        os::Linux::heap_dict_free(_redact_rules_table, true);
        _redact_rules_table = NULL;
    }
    if (_replace_value_table != NULL) {
        os::Linux::heap_dict_free(_replace_value_table, false);
        _replace_value_table = NULL;
    }
    if(_redact_class_field_table != NULL) {
        os::Linux::heap_dict_free(_redact_class_field_table, true);
        _redact_class_field_table = NULL;
    }
    if(_redact_record != NULL) {
        os::Linux::heap_vector_free(_redact_record);
        _redact_record = NULL;
    }
#endif
    if (_file_name_map_list != NULL) {
        FREE_C_HEAP_ARRAY(char, _file_name_map_list, mtInternal);
    }
    if (_name_map_list != NULL) {
        FREE_C_HEAP_ARRAY(char, _name_map_list, mtInternal);
    }
    if (_redact_params.params_string != NULL) {
        FREE_C_HEAP_ARRAY(char, _redact_params.params_string, mtInternal);
    }
    if(_annotation_class_path != NULL) {
        FREE_C_HEAP_ARRAY(char, _annotation_class_path, mtInternal);
    }
}

void HeapRedactor::init_fields() {
    _redact_level = REDACT_UNKNOWN;
    _redact_name_table = NULL;
    _redact_rules_table= NULL;
    _replace_value_table = NULL;
    _redact_class_field_table = NULL;
    _file_name_map_list = NULL;
    _name_map_list = NULL;
    _redact_class_full_name = NULL;
    _annotation_class_path = NULL;
    _redact_record = NULL;
    _redact_params.params_string = NULL;
    _redact_params.heap_dump_redact = NULL;
    _redact_params.redact_map = NULL;
    _redact_params.redact_map_file = NULL;
    _redact_params.annotation_class_path = NULL;
    _redact_params.redact_password = NULL;
}

void HeapRedactor::parse_redact_params(const char *redact_params_string) {
    size_t length = strlen(redact_params_string);
    char* buf = NEW_C_HEAP_ARRAY(char, length + 1, mtInternal);
    _redact_params.params_string = buf;
    strncpy(_redact_params.params_string, redact_params_string, length + 1);
    size_t start = strlen("-HeapDumpRedact=");
    _redact_params.heap_dump_redact = _redact_params.params_string + start;
    char* map_pos = strstr(_redact_params.heap_dump_redact, ",RedactMap=");
    char* file_pos = strstr(_redact_params.heap_dump_redact, ",RedactMapFile=");
    char* class_path_pos = strstr(_redact_params.heap_dump_redact, ",RedactClassPath=");
    char* redact_password_pos = strstr(_redact_params.heap_dump_redact, ",RedactPassword=");

    _redact_params.redact_map = parse_redact_child_param(map_pos, ",RedactMap=",
                                                         file_pos);
    _redact_params.redact_map_file = parse_redact_child_param(file_pos, ",RedactMapFile=",
                                                              class_path_pos);
    _redact_params.annotation_class_path = parse_redact_child_param(class_path_pos, ",RedactClassPath=",
                                                                    redact_password_pos);
    _redact_params.redact_password = parse_redact_child_param(redact_password_pos, ",RedactPassword=",
                                                              _redact_params.params_string + length);
}

char* HeapRedactor::parse_redact_child_param(char *redact_params_sub_string, const char* redact_param_prefix,
                                             const char* next_redact_param_prefix) {
    char* pos = NULL;
    if (redact_params_sub_string == NULL) {
        pos = NULL;
    } else {
        *redact_params_sub_string = '\0';
        pos = redact_params_sub_string + strlen(redact_param_prefix);
        if (pos == next_redact_param_prefix) {
            pos = NULL;
        }
    }
    return pos;
}

bool HeapRedactor::check_launcher_heapdump_redact_support(const char *value) {
    if (!strcmp(value, "=basic") || !strcmp(value, "=names") || !strcmp(value, "=off")
        || !strcmp(value, "=diyrules") ||!strcmp(value, "=annotation") || !strcmp(value, "=full")) {
        return true;
    }
    return false;
}

void HeapRedactor::init(outputStream* out) {
    /** -XX:+VerifyRedactPassword,
   * if HeapDumpRedact is NULL , jmap operation can not open redact feature without password
   * if HeapDumpRedact is not NULL, jmap operation can not change redact level without password
   **/
    char* split_char = NULL;
    if(RedactPassword == NULL || (split_char = strstr(const_cast<char*>(RedactPassword), ",")) == NULL || strlen(split_char) < SALT_LEN) {
        VerifyRedactPassword = false;
    }
    if(VerifyRedactPassword && !_use_sys_params) {
        size_t auth_len = strlen(RedactPassword);
        size_t suffix_len = strlen(split_char);
        if(_redact_params.redact_password == NULL ||
           strncmp(_redact_params.redact_password, RedactPassword, auth_len-suffix_len) ) {
            // no password or wrong password;
            _use_sys_params = true;
            if(out != NULL) {
                out->print_cr("not correct password, use the default redact mode when stared");
            }
        }
    }

    if(_redact_params.redact_password != NULL) {
        size_t password_Len = strlen(_redact_params.redact_password);
        memset(_redact_params.redact_password, '\0', password_Len);
    }

    if (_redact_level == REDACT_UNKNOWN) {
        init_heapdump_redact_level();
    }
    return;
}

void HeapRedactor::init_redact_map() {
    const char* map_param = NULL;
    const char* map_file_param = NULL;
    if (_use_sys_params) {
        map_param = RedactMap;
        map_file_param = RedactMapFile;
    } else {
        map_param = _redact_params.redact_map;
        map_file_param = _redact_params.redact_map_file;
    }
    if (map_file_param != NULL) {
        read_redact_map_from_file(map_file_param);
    }
    if (map_param != NULL) {
        size_t length = strlen(map_param);
        _name_map_list = NEW_C_HEAP_ARRAY(char, length + 1, mtInternal);
        strncpy(_name_map_list, map_param, length + 1);
        read_redact_map_dependon_mode(_name_map_list, _redact_level);
    }
}

void HeapRedactor::read_redact_map_dependon_mode(char* name_map_list, HeapDumpRedactLevel redact_level) {
    if(redact_level == REDACT_DIYRULES) {
        parse_redact_diy_rules(name_map_list);
    } else {
        parse_redact_map_string(name_map_list);
    }
}

void HeapRedactor::parse_redact_map_string(char *name_map_list) {
#ifdef LINUX
    size_t token_start = 0;
    size_t step = 0;
    size_t length = strlen(name_map_list);

    while (step < length) {
        bool is_seperator = false;
        if ((is_seperator = (name_map_list[step] == ',' || name_map_list[step] == ';' || name_map_list[step] == '\n' ||
            name_map_list[step] == ' ')) ||
            step == length - 1) {
            if (is_seperator) {
                name_map_list[step] = '\0';
            } else {
                step++;
            }
            if (token_start < step) {
                char *token = name_map_list + token_start;
                size_t i = 0;
                size_t token_length = strlen(token);
                while (i < token_length && token[i] != ':') {
                    i++;
                }
                if (i < token_length - 1) {
                    token[i] = '\0';
                    if((strlen(token) < INT_MAX) && (strlen(token + i + 1) < INT_MAX)) {
                        _redact_name_table = os::Linux::heap_dict_add(token, token + i + 1, _redact_name_table, 0);
                    }
                }
            }
            token_start = step + 1;
        }
        step++;
    }
#endif
}

void HeapRedactor::read_redact_map_from_file(const char *path) {
    char base_path[JVM_MAXPATHLEN] = {'\0'};
    char buffer[MAX_MAP_FILE_LENGTH + 1] = {'\0'};
    if (path == NULL || path[0] == '\0') {
        // RedactMapFile=<file> not specified
    } else {
        if (strlen(path) >= JVM_MAXPATHLEN) {
            warning("RedactMap File path is too long ");
            return;
        }
        strncpy(base_path, path, sizeof(base_path));
        // check if the path is a directory (must exist)
        int fd = open(base_path, O_RDONLY);
        if (fd == -1) {
            return;
        }
        size_t num_read = os::read(fd, (char *)buffer, MAX_MAP_FILE_LENGTH);

        _file_name_map_list = NEW_C_HEAP_ARRAY(char, num_read + 1, mtInternal);
        strncpy(_file_name_map_list, buffer, num_read + 1);

        read_redact_map_dependon_mode(_file_name_map_list, _redact_level);
    }
}

void HeapRedactor::parse_redact_diy_rules(char* name_map_list) {
    size_t token_start = 0;
    size_t step = 0;
    size_t length = strlen(name_map_list);

    while (step < length) {
        bool is_seperator = false;
        if ((is_seperator = (name_map_list[step] == ',' || name_map_list[step] == ';' || name_map_list[step] == '\n' ||
                             name_map_list[step] == ' ')) ||
            step == length - 1) {
            if (is_seperator) {
                name_map_list[step] = '\0';
            } else {
                step++;
            }

            if (token_start >= step) {
                // to reduce the depth of the method
                token_start = step + 1;
                continue;
            }

            char *token = name_map_list + token_start;
            parse_token(token);
            token_start = step + 1;
        }
        step++;
    }
    // clear _redact_class_full_name, encase RedactMap has an unformatted value(without class name),
    // will rewrite  the last class's value_map
    _redact_class_full_name = NULL;
}

void HeapRedactor::parse_token(char* token) {
#ifdef LINUX
    size_t i = 0;
    size_t token_length = strlen(token);
    while (i < token_length && token[i] != ':') {
        if(token[i] == '.' ) {
            token[i] = '/';
        }
        i++;
    }

    void* _redact_rules_sub_table = _redact_class_full_name == NULL ? NULL :
                                    os::Linux::heap_dict_lookup(_redact_class_full_name, _redact_rules_table, false);
    if (i < token_length - 1 && _redact_rules_sub_table != NULL) {
        token[i] = '\0';
        os::Linux::heap_dict_add(token, token + i + 1, _redact_rules_sub_table, 0);
    } else if( i == token_length) {
        _redact_class_full_name = token;
        _redact_rules_sub_table = os::Linux::heap_dict_lookup(token, _redact_rules_table, false);
        if (_redact_rules_sub_table == NULL) {
            _redact_rules_sub_table = os::Linux::heap_dict_add(token, NULL, _redact_rules_sub_table, 0);
            _redact_rules_table = os::Linux::heap_dict_add(token, _redact_rules_sub_table, _redact_rules_table, 0);
        }
    }
#endif
}

HeapDumpRedactLevel HeapRedactor::init_heapdump_redact_level() {
    const char* redact_string = NULL;
    if (_use_sys_params) {
        redact_string = HeapDumpRedact;
    } else {
        redact_string = _redact_params.heap_dump_redact;
    }
    if (redact_string == NULL) {
        _redact_level = REDACT_OFF;
    } else {
#ifdef LINUX
        if (strcmp(redact_string, "basic") == 0) {
            _redact_level = REDACT_BASIC;
        } else if (strcmp(redact_string, "names") == 0) {
            _redact_level = REDACT_NAMES;
            init_redact_map();
        } else if (strcmp(redact_string, "full") == 0) {
            _redact_level = REDACT_FULL;
            init_redact_map();
        } else if (strcmp(redact_string, "diyrules") == 0) {
            _redact_level = REDACT_DIYRULES;
            init_redact_map();
        } else if (strcmp(redact_string, "annotation") == 0) {
            _redact_level = REDACT_ANNOTATION;
            init_class_path();
            if(_annotation_class_path == NULL) {
                _redact_level = REDACT_OFF;
            }
        } else {
            _redact_level = REDACT_OFF;
        }
#else
        if (strcmp(redact_string, "basic") == 0) {
            _redact_level = REDACT_BASIC;
        } else if (strcmp(redact_string, "full") == 0) {
            _redact_level = REDACT_BASIC;
        } else {
            _redact_level = REDACT_OFF;
        }
#endif
    }
    return _redact_level;
}

void HeapRedactor::init_class_path() {
    const char* class_path = NULL;
    if (_use_sys_params) {
        class_path = RedactClassPath;
    } else {
        class_path = _redact_params.annotation_class_path;
    }

    if(class_path != NULL) {
        size_t class_path_len = strlen(class_path);
        _annotation_class_path = NEW_C_HEAP_ARRAY(char, class_path_len + 3, mtInternal);
        _annotation_class_path[0] = 'L';
        strncpy(_annotation_class_path + 1, class_path, class_path_len + 1);
        _annotation_class_path[class_path_len + 1] = ';';
        _annotation_class_path[class_path_len + 2] = '\0';
    }
}

void HeapRedactor::insert_anonymous_value(void* key, void* value){
#ifdef LINUX
    _replace_value_table = os::Linux::heap_dict_add(key, value, _replace_value_table, 1);
#endif
}

bool HeapRedactor::lookup_annotation_index_in_constant_pool(AnnotationArray* field_annotations, ConstantPool *cp, int &byte_i_ref) {
    u2 num_annotations = 0;
    bool has_anonymous_annotation = false;

    if ((byte_i_ref + 2) > field_annotations->length()) {
        // not enough room for num_annotations field
        return false;
    } else {
        num_annotations = Bytes::get_Java_u2((address) field_annotations->adr_at(byte_i_ref));
    }

    byte_i_ref += 2;

    for (int calc_num_annotations = 0; calc_num_annotations < num_annotations; calc_num_annotations++) {

        if ((byte_i_ref + 2 + 2) > field_annotations->length()) {
            // not enough room for smallest annotation_struct
            return false;
        }

        // get constants pool index
        address cp_index_addr = (address) field_annotations->adr_at(byte_i_ref);
        byte_i_ref += 2;
        u2 cp_index = Bytes::get_Java_u2(cp_index_addr);
        if (cp_index >= cp->tags()->length()) {
            return false;
        }
        Symbol *annotate_class_symbol = cp->symbol_at(cp_index);
        char *annotate_class_name = annotate_class_symbol->as_C_string();

        u2 num_element_value_pairs = Bytes::get_Java_u2((address) field_annotations->adr_at(byte_i_ref));
        byte_i_ref += 2;
        if ((byte_i_ref + 2 + 1) > field_annotations->length()) {
            // not enough room for smallest annotation_struct
            return false;
        }

        const char *annotation_class_path = get_annotation_class_path();
        has_anonymous_annotation = (strcmp(annotation_class_path, annotate_class_name) == 0);
        if (has_anonymous_annotation) {
            address element_name_addr = (address) field_annotations->adr_at(byte_i_ref);
            byte_i_ref += 2;
            u2 cp_name_index = Bytes::get_Java_u2(element_name_addr);
            Symbol *element_name_symbol = cp->symbol_at(cp_name_index);
            char *element_name = element_name_symbol->as_C_string();
            if(element_name == NULL || strcmp(element_name, "value")) {
                // expected annotation has only one field "value"
                return false;
            }
            // skip element tag
            byte_i_ref++;
            return true;
        }

        int calc_num_element_value_pairs = 0;
        // skip element_name_index
        byte_i_ref += 2;
        for (; calc_num_element_value_pairs < num_element_value_pairs; calc_num_element_value_pairs++) {
            if (!recursion_cp_refs_in_element_value(field_annotations, byte_i_ref)) {
                return false;
            }
        }
    }
    return false;
}

bool HeapRedactor::recursion_cp_refs_in_annotation_struct(
        AnnotationArray* annotations_typeArray, int &byte_i_ref) {
    if ((byte_i_ref + 2 + 2) > annotations_typeArray->length()) {
        // not enough room for smallest annotation_struct
        return false;
    }

    u2 type_index = Bytes::get_Java_u2((address)annotations_typeArray->adr_at(byte_i_ref));
    byte_i_ref += 2;

    u2 num_element_value_pairs = Bytes::get_Java_u2((address) annotations_typeArray->adr_at(byte_i_ref));
    byte_i_ref += 2;

    int calc_num_element_value_pairs = 0;
    for (; calc_num_element_value_pairs < num_element_value_pairs;
           calc_num_element_value_pairs++) {
        if ((byte_i_ref + 2) > annotations_typeArray->length()) {
            // not enough room for another element_name_index, let alone
            // the rest of another component
            // length() is too small for element_name_index
            return false;
        }

        u2 element_name_index = Bytes::get_Java_u2((address) annotations_typeArray->adr_at(byte_i_ref));
        byte_i_ref += 2;

        if (!recursion_cp_refs_in_element_value(annotations_typeArray, byte_i_ref)) {
            // bad element_value
            // propagate failure back to caller
            return false;
        }
    } // end for each component
    assert(num_element_value_pairs == calc_num_element_value_pairs, "sanity check");

    return true;
} // end recursion_cp_refs_in_annotation_struct()

bool HeapRedactor::recursion_cp_refs_in_element_value(AnnotationArray* field_annotations, int &byte_i_ref) {
    if ((byte_i_ref + 1) > field_annotations->length()) {
        // not enough room for a tag let alone the rest of an element_value
        return false;
    }

    u1 tag = field_annotations->at(byte_i_ref);
    byte_i_ref++;
    switch (tag) {
        case JVM_SIGNATURE_BYTE:
        case JVM_SIGNATURE_CHAR:
        case JVM_SIGNATURE_DOUBLE:
        case JVM_SIGNATURE_FLOAT:
        case JVM_SIGNATURE_INT:
        case JVM_SIGNATURE_LONG:
        case JVM_SIGNATURE_SHORT:
        case JVM_SIGNATURE_BOOLEAN:
        case 's':
        case 'c':
        {
            if ((byte_i_ref + 2) > field_annotations->length()) {
                // length() is too small for a const_value_index
                break;
            }
            byte_i_ref += 2;
        } break;
        case 'e':
        {
            if ((byte_i_ref + 4) > field_annotations->length()) {
                // length() is too small for a enum_const_value
                break;
            }
            byte_i_ref += 4;
        } break;

        case '@':
            // For the above tag value, value.attr_value is the right union
            // field. This is a nested annotation.
            if (!recursion_cp_refs_in_annotation_struct(field_annotations, byte_i_ref)) {
                // propagate failure back to caller
                return false;
            }
            break;

        case JVM_SIGNATURE_ARRAY:
        {
            if ((byte_i_ref + 2) > field_annotations->length()) {
                // not enough room for a num_values field
                return false;
            }

            // For the above tag value, value.array_value is the right union
            // field. This is an array of nested element_value.
            u2 num_values = Bytes::get_Java_u2((address) field_annotations->adr_at(byte_i_ref));
            byte_i_ref += 2;

            int calc_num_values = 0;
            for (; calc_num_values < num_values; calc_num_values++) {
                if (!recursion_cp_refs_in_element_value(field_annotations, byte_i_ref)) {
                    // bad nested element_value
                    // propagate failure back to caller
                    return false;
                }
            }
        } break;

        default:
            // bad tag
            return false;
    }

    return true;
}

bool HeapRedactor::record_typeArrayOop(typeArrayOop array) {
    bool _inserted = false;
#ifdef LINUX
    _redact_record = os::Linux::heap_vector_add(array, _redact_record, _inserted);
#endif
    return _inserted;
}


void HeapRedactor::insert_class_field_value(void* class_key, void* field_key, void* value) {
#ifdef LINUX
    void* _redact_sub_table = os::Linux::heap_dict_lookup(class_key, _redact_class_field_table, false);
    _redact_sub_table = os::Linux::heap_dict_add(field_key, value, _redact_sub_table, 1);
    _redact_class_field_table = os::Linux::heap_dict_add(class_key, _redact_sub_table, _redact_class_field_table, 1);
#endif
}
