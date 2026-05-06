/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
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
 */

#include "matrix/matrixAllowList.hpp"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "classfile/symbolTable.hpp"
#include "matrix/matrixLog.hpp"
#include "memory/resourceArea.hpp"
#include "runtime/thread.hpp"
#include "runtime/vframe.hpp"

AllowListEntry* AllowListTable::bucket_at(int index) {
  return (AllowListEntry*)Hashtable<Symbol*, mtClass>::bucket(index);
}

void AllowListTable::add(Symbol* class_name, Symbol* method_name) {
  unsigned int hash = class_name->identity_hash();
  AllowListEntry* entry =
      (AllowListEntry*)Hashtable<Symbol*, mtClass>::new_entry(hash, class_name);
  entry->set_method_name(method_name);
  add_entry(hash_to_index(hash), entry);
}

bool AllowListTable::contains(Symbol* class_name, Symbol* method_name) {
  int index = hash_to_index(class_name->identity_hash());
  for (AllowListEntry* entry = bucket_at(index); entry != NULL; entry = entry->next()) {
    if (entry->class_name()->fast_compare(class_name) == 0 &&
        entry->method_name()->fast_compare(method_name) == 0) {
      return true;
    }
  }
  return false;
}

static char* trim_allow_list_line(char* line) {
  while (*line != '\0' && isspace((unsigned char)*line)) {
    line++;
  }
  char* end = line + strlen(line);
  while (end > line && isspace((unsigned char)*(end - 1))) {
    end--;
  }
  *end = '\0';
  return line;
}

static bool is_java_identifier_start(char ch) {
  return isalpha((unsigned char)ch) || ch == '_' || ch == '$';
}

static bool is_java_identifier_part(char ch) {
  return isalnum((unsigned char)ch) || ch == '_' || ch == '$';
}

static bool is_valid_java_name(const char* name) {
  if (name == NULL || *name == '\0') {
    return false;
  }
  const char* part = name;
  while (*part != '\0') {
    if (!is_java_identifier_start(*part)) {
      return false;
    }
    part++;
    while (*part != '\0' && *part != '/') {
      if (!is_java_identifier_part(*part)) {
        return false;
      }
      part++;
    }
    if (*part == '/') {
      part++;
      if (*part == '\0') {
        return false;
      }
    }
  }
  return true;
}

static bool is_valid_method_name(const char* name) {
  if (name == NULL || *name == '\0') {
    return false;
  }
  if (strcmp(name, "<init>") == 0 || strcmp(name, "<clinit>") == 0) {
    return true;
  }
  if (!is_java_identifier_start(*name)) {
    return false;
  }
  for (const char* ch = name + 1; *ch != '\0'; ch++) {
    if (!is_java_identifier_part(*ch)) {
      return false;
    }
  }
  return true;
}

static bool parse_allow_list_entry(char* line, char** class_name, char** method_name) {
  char* dot = strrchr(line, '.');
  if (dot == NULL || dot == line || *(dot + 1) == '\0') {
    return false;
  }
  *dot = '\0';
  *class_name = line;
  *method_name = dot + 1;
  return is_valid_java_name(*class_name) && is_valid_method_name(*method_name);
}

static bool load_allow_list_entry(AllowListTable* table, UBFeature feature, const char* conf_path,
                                  int line_no, char* line) {
  char* entry = trim_allow_list_line(line);
  if (*entry == '\0' || *entry == '#') {
    return false;
  }

  char original_entry[256];
  strncpy(original_entry, entry, sizeof(original_entry) - 1);
  original_entry[sizeof(original_entry) - 1] = '\0';

  char* class_name = NULL;
  char* method_name = NULL;
  if (!parse_allow_list_entry(entry, &class_name, &method_name)) {
    UB_LOG(feature, UB_LOG_WARNING, "Ignore invalid allow-list entry %s:%d: %s\n",
           conf_path, line_no, original_entry);
    return false;
  }

  Symbol* class_symbol =
      SymbolTable::lookup(class_name, (int)strlen(class_name), Thread::current());
  Symbol* method_symbol =
      SymbolTable::lookup(method_name, (int)strlen(method_name), Thread::current());
  UB_LOG(feature, UB_LOG_INFO, "Load allow method: %s.%s\n", class_symbol->as_C_string(),
         method_symbol->as_C_string());
  table->add(class_symbol, method_symbol);
  return true;
}

int AllowListTable::load_from_file(const char* conf_path) {
  FILE* conf_file = fopen(conf_path, "r");
  int allow_method_count = 0;
  if (conf_file == NULL) { return 0; }
  UB_LOG(_feature, UB_LOG_INFO, "Load conf file: %s\n", conf_path);

  char line[256];
  int line_no = 0;
  while (fgets(line, sizeof(line), conf_file) != NULL) {
    line_no++;
    line[strcspn(line, "\n")] = '\0';
    if (load_allow_list_entry(this, _feature, conf_path, line_no, line)) {
      allow_method_count++;
    }
  }

  if (fclose(conf_file) != 0) {
    UB_LOG(_feature, UB_LOG_WARNING, "fclose %s failed: %s\n",
           conf_path, strerror(errno));
  }
  return allow_method_count;
}

bool AllowListTable::check_stack() {
  JavaThread* jt = JavaThread::current();
  if (!jt->has_last_Java_frame()) return false;  // no Java frames

  ResourceMark rm;
  RegisterMap reg_map(jt);
  javaVFrame* jvf = jt->last_java_vframe(&reg_map);
  Method* last_method = jvf->method();
  int n = 0;
  while (jvf != NULL) {
    Method* method = jvf->method();
    if (contains(method->klass_name(), method->name())) { return true; }
    jvf = jvf->java_sender();
    n++;
    if (MaxJavaStackTraceDepth == n) break;
  }
  return false;
}
