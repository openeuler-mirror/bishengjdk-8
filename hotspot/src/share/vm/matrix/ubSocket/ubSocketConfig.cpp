/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 */

#include "precompiled.hpp"
#include "matrix/ubSocket/ubSocketConfig.hpp"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "classfile/symbolTable.hpp"
#include "matrix/matrixLog.hpp"
#include "matrix/ubSocket/ubSocketFrame.hpp"
#include "runtime/thread.hpp"
#include "utilities/ostream.hpp"

static const int UB_CONFIG_LINE_LEN = 256;
static const int UB_CONFIG_DECIMAL_BASE = 10;
static const size_t UB_CONFIG_IPV4_LEN = sizeof(struct in_addr);
static const size_t UB_CONFIG_V4_MAPPED_OFFSET =
    sizeof(struct in6_addr) - sizeof(struct in_addr);

struct UBSocketConfig::StackEntry : public CHeapObj<mtInternal> {
  Symbol* class_name;
  Symbol* method_name;
  StackEntry* next;
};

struct UBSocketConfig::SelfPortEntry : public CHeapObj<mtInternal> {
  uint16_t public_port;
  uint16_t local_port;
  SelfPortEntry* next;
};

struct UBSocketConfig::RemoteMappingEntry : public CHeapObj<mtInternal> {
  UBSocketEndpoint public_data;
  UBSocketEndpoint public_control;
  RemoteMappingEntry* next;
};

static char* config_trim(char* value) {
  while (*value != '\0' && isspace((unsigned char)*value)) { value++; }
  char* end = value + strlen(value);
  while (end > value && isspace((unsigned char)*(end - 1))) { end--; }
  *end = '\0';
  return value;
}

static void config_error(char* error, size_t error_len, int line_no,
                         const char* reason, const char* entry) {
  if (error == NULL || error_len == 0) { return; }
  if (line_no > 0) {
    jio_snprintf(error, error_len, "line %d: %s: %s", line_no, reason,
                 entry == NULL ? "" : entry);
  } else {
    jio_snprintf(error, error_len, "%s", reason);
  }
}

static bool config_parse_port(const char* value, uint16_t* port) {
  char* end = NULL;
  long parsed = strtol(value, &end, UB_CONFIG_DECIMAL_BASE);
  if (end == value || *end != '\0' || parsed <= 0 || parsed > UB_SOCKET_PORT_MAX) {
    return false;
  }
  *port = (uint16_t)parsed;
  return true;
}

static bool config_parse_endpoint(char* value, bool allow_wildcard,
                                  UBSocketEndpoint* endpoint, bool* wildcard) {
  *wildcard = false;
  char* host = value;
  char* port_string = NULL;
  if (*host == '[') {
    char* close = strchr(host, ']');
    if (close == NULL || close[1] != ':') { return false; }
    *close = '\0';
    host++;
    port_string = close + strlen("]:");
  } else {
    char* colon = strrchr(host, ':');
    if (colon == NULL || colon == host || colon[1] == '\0') { return false; }
    *colon = '\0';
    port_string = colon + 1;
  }

  uint16_t port = 0;
  if (!config_parse_port(port_string, &port)) { return false; }
  memset(endpoint, 0, sizeof(*endpoint));
  endpoint->port = port;
  if (strcmp(host, "*") == 0) {
    if (!allow_wildcard) { return false; }
    endpoint->family = AF_UNSPEC;
    *wildcard = true;
    return true;
  }
  endpoint->family = AF_INET;
  if (inet_pton(AF_INET, host, endpoint->addr) == 1) { return true; }
  endpoint->family = AF_INET6;
  return inet_pton(AF_INET6, host, endpoint->addr) == 1;
}

static bool config_parse_ip(char* value, bool allow_wildcard,
                            UBSocketEndpoint* endpoint, bool* wildcard) {
  char endpoint_buffer[UB_CONFIG_LINE_LEN];
  jio_snprintf(endpoint_buffer, sizeof(endpoint_buffer), "%s:1", value);
  return config_parse_endpoint(endpoint_buffer, allow_wildcard, endpoint, wildcard);
}

static bool config_split_mapping(char* value, char** lhs, char** rhs) {
  char* arrow = strstr(value, "->");
  if (arrow == NULL) { return false; }
  *arrow = '\0';
  *lhs = config_trim(value);
  *rhs = config_trim(arrow + strlen("->"));
  return **lhs != '\0' && **rhs != '\0';
}

static bool config_ip_equals(const UBSocketEndpoint* lhs,
                             const UBSocketEndpoint* rhs) {
  if (lhs->family == rhs->family) {
    return memcmp(lhs->addr, rhs->addr, sizeof(lhs->addr)) == 0;
  }
  if (lhs->family == AF_INET && rhs->family == AF_INET6 &&
      IN6_IS_ADDR_V4MAPPED((const struct in6_addr*)rhs->addr)) {
    return memcmp(lhs->addr, rhs->addr + UB_CONFIG_V4_MAPPED_OFFSET,
                  UB_CONFIG_IPV4_LEN) == 0;
  }
  if (lhs->family == AF_INET6 && rhs->family == AF_INET &&
      IN6_IS_ADDR_V4MAPPED((const struct in6_addr*)lhs->addr)) {
    return memcmp(lhs->addr + UB_CONFIG_V4_MAPPED_OFFSET, rhs->addr,
                  UB_CONFIG_IPV4_LEN) == 0;
  }
  return false;
}

static bool config_valid_java_name(const char* name) {
  if (name == NULL || *name == '\0') { return false; }
  const char* part = name;
  while (*part != '\0') {
    if (!(isalpha((unsigned char)*part) || *part == '_' || *part == '$')) {
      return false;
    }
    part++;
    while (*part != '\0' && *part != '/') {
      if (!(isalnum((unsigned char)*part) || *part == '_' || *part == '$')) {
        return false;
      }
      part++;
    }
    if (*part == '/') {
      part++;
      if (*part == '\0') { return false; }
    }
  }
  return true;
}

static bool config_valid_method_name(const char* name) {
  if (name == NULL || *name == '\0') { return false; }
  if (strcmp(name, "<init>") == 0 || strcmp(name, "<clinit>") == 0) { return true; }
  if (!(isalpha((unsigned char)*name) || *name == '_' || *name == '$')) { return false; }
  for (const char* p = name + 1; *p != '\0'; p++) {
    if (!(isalnum((unsigned char)*p) || *p == '_' || *p == '$')) { return false; }
  }
  return true;
}

static bool config_parse_stack(char* value, char** class_name, char** method_name) {
  char* dot = strrchr(value, '.');
  if (dot == NULL || dot == value || dot[1] == '\0') { return false; }
  *dot = '\0';
  *class_name = value;
  *method_name = dot + 1;
  return config_valid_java_name(*class_name) && config_valid_method_name(*method_name);
}

static void config_copy_path(char** destination, const char* path) {
  size_t length = strlen(path);
  *destination = NEW_C_HEAP_ARRAY(char, length + 1, mtInternal);
  memcpy(*destination, path, length + 1);
}

UBSocketConfig::UBSocketConfig(const char* source_path)
    : _source_path(NULL), _stack_head(NULL), _stack_tail(NULL),
      _self_port_head(NULL), _self_port_tail(NULL), _remote_head(NULL),
      _remote_tail(NULL), _self_public_ip(NULL), _self_local_ip(NULL),
      _self_local_ip_wildcard(false), _has_self_ip(false),
      _has_self_mapping(false), _stack_count(0), _self_port_count(0),
      _remote_mapping_count(0) {
  config_copy_path(&_source_path, source_path);
}

UBSocketConfig::~UBSocketConfig() {
  StackEntry* stack = _stack_head;
  while (stack != NULL) {
    StackEntry* next = stack->next;
    if (stack->class_name != NULL) { stack->class_name->decrement_refcount(); }
    if (stack->method_name != NULL) { stack->method_name->decrement_refcount(); }
    delete stack;
    stack = next;
  }
  SelfPortEntry* port = _self_port_head;
  while (port != NULL) {
    SelfPortEntry* next = port->next;
    delete port;
    port = next;
  }
  RemoteMappingEntry* remote = _remote_head;
  while (remote != NULL) {
    RemoteMappingEntry* next = remote->next;
    delete remote;
    remote = next;
  }
  if (_self_public_ip != NULL) {
    FREE_C_HEAP_ARRAY(UBSocketEndpoint, _self_public_ip, mtInternal);
  }
  if (_self_local_ip != NULL) {
    FREE_C_HEAP_ARRAY(UBSocketEndpoint, _self_local_ip, mtInternal);
  }
  if (_source_path != NULL) { FREE_C_HEAP_ARRAY(char, _source_path, mtInternal); }
}

UBSocketConfig* UBSocketConfig::load(const char* path, bool strict,
                                     char* error, size_t error_len) {
  if (path == NULL || *path == '\0') {
    config_error(error, error_len, 0, "empty configuration path", NULL);
    return NULL;
  }
  FILE* file = fopen(path, "r");
  if (file == NULL) {
    config_error(error, error_len, 0, strerror(errno), path);
    return NULL;
  }

  UB_LOG(UB_SOCKET, UB_LOG_INFO, "Load conf file: %s\n", path);
  UBSocketConfig* config = new UBSocketConfig(path);
  enum Section { NONE, STACK, ADDRESS } section = NONE;
  bool saw_invalid = false;
  char line[UB_CONFIG_LINE_LEN];
  int line_no = 0;
  while (fgets(line, sizeof(line), file) != NULL) {
    line_no++;
    if (strchr(line, '\n') == NULL && !feof(file)) {
      char discard[UB_CONFIG_LINE_LEN];
      while (fgets(discard, sizeof(discard), file) != NULL && strchr(discard, '\n') == NULL) {}
      config_error(error, error_len, line_no, "line too long", line);
      saw_invalid = true;
      continue;
    }
    line[strcspn(line, "\n")] = '\0';
    char* entry = config_trim(line);
    if (strcmp(entry, "#stack") == 0) {
      section = STACK;
      continue;
    }
    if (strcmp(entry, "#address") == 0) {
      section = ADDRESS;
      continue;
    }
    if (*entry == '\0' || *entry == '#') { continue; }

    char original[UB_CONFIG_LINE_LEN];
    strncpy(original, entry, sizeof(original) - 1);
    original[sizeof(original) - 1] = '\0';
    bool valid = false;
    if (section == STACK) {
      char* class_name = NULL;
      char* method_name = NULL;
      if (config_parse_stack(entry, &class_name, &method_name)) {
        StackEntry* item = new StackEntry();
        item->class_name = SymbolTable::lookup(class_name, (int)strlen(class_name),
                                               JavaThread::current());
        item->method_name = SymbolTable::lookup(method_name, (int)strlen(method_name),
                                                JavaThread::current());
        item->next = NULL;
        if (config->_stack_tail == NULL) config->_stack_head = item;
        else config->_stack_tail->next = item;
        config->_stack_tail = item;
        config->_stack_count++;
        UB_LOG(UB_SOCKET, UB_LOG_INFO, "Load allow method: %s.%s\n",
               item->class_name->as_C_string(), item->method_name->as_C_string());
        valid = true;
      }
    } else if (section == ADDRESS) {
      char* equal = strchr(entry, '=');
      if (equal != NULL) {
        *equal = '\0';
        char* key = config_trim(entry);
        char* value = config_trim(equal + 1);
        if (strcmp(key, "self.ip") == 0) {
          char* public_ip = NULL;
          char* local_ip = NULL;
          bool public_wildcard = false;
          bool local_wildcard = false;
          UBSocketEndpoint public_endpoint;
          UBSocketEndpoint local_endpoint;
          valid = config_split_mapping(value, &public_ip, &local_ip) &&
                  config_parse_ip(public_ip, false, &public_endpoint, &public_wildcard) &&
                  config_parse_ip(local_ip, true, &local_endpoint, &local_wildcard) &&
                  !config->_has_self_ip;
          if (valid) {
            config->_self_public_ip = NEW_C_HEAP_ARRAY(UBSocketEndpoint, 1, mtInternal);
            config->_self_local_ip = NEW_C_HEAP_ARRAY(UBSocketEndpoint, 1, mtInternal);
            *config->_self_public_ip = public_endpoint;
            *config->_self_local_ip = local_endpoint;
            config->_self_local_ip_wildcard = local_wildcard;
            config->_has_self_ip = true;
            UB_LOG(UB_SOCKET, UB_LOG_INFO, "Load self ip map\n");
          }
        } else if (strcmp(key, "self.port") == 0) {
          char* public_port_string = NULL;
          char* local_port_string = NULL;
          uint16_t public_port = 0;
          uint16_t local_port = 0;
          valid = config_split_mapping(value, &public_port_string, &local_port_string) &&
                  config_parse_port(public_port_string, &public_port) &&
                  config_parse_port(local_port_string, &local_port);
          if (valid) {
            SelfPortEntry* item = new SelfPortEntry();
            item->public_port = public_port;
            item->local_port = local_port;
            item->next = NULL;
            if (config->_self_port_tail == NULL) config->_self_port_head = item;
            else config->_self_port_tail->next = item;
            config->_self_port_tail = item;
            config->_self_port_count++;
            UB_LOG(UB_SOCKET, UB_LOG_INFO,
                   "Load self port map public_port=%u local_port=%u\n",
                   public_port, local_port);
          }
        } else if (strcmp(key, "remote.mapping") == 0) {
          char* public_data = NULL;
          char* public_control = NULL;
          RemoteMappingEntry* item = new RemoteMappingEntry();
          bool wildcard = false;
          valid = config_split_mapping(value, &public_data, &public_control) &&
                  config_parse_endpoint(public_data, false, &item->public_data, &wildcard) &&
                  config_parse_endpoint(public_control, false, &item->public_control, &wildcard);
          if (valid) {
            item->next = NULL;
            if (config->_remote_tail == NULL) config->_remote_head = item;
            else config->_remote_tail->next = item;
            config->_remote_tail = item;
            config->_remote_mapping_count++;
            UB_LOG(UB_SOCKET, UB_LOG_INFO,
                   "Load remote endpoint map data_port=%u control_port=%u\n",
                   item->public_data.port, item->public_control.port);
          } else {
            delete item;
          }
        }
      }
    }

    if (!valid) {
      if (strict) {
        config_error(error, error_len, line_no, "invalid configuration entry", original);
        saw_invalid = true;
        break;
      }
      UB_LOG(UB_SOCKET, UB_LOG_WARNING, "Ignore invalid configuration %s:%d: %s\n",
             path, line_no, original);
    }
  }
  int read_errno = ferror(file) ? errno : 0;
  bool close_ok = fclose(file) == 0;
  if (read_errno != 0 && !saw_invalid) {
    config_error(error, error_len, line_no, strerror(read_errno), path);
    saw_invalid = true;
  } else if (!close_ok && !saw_invalid) {
    config_error(error, error_len, line_no, strerror(errno), path);
    saw_invalid = true;
  }
  if (!saw_invalid && strict && config->_stack_count == 0) {
    config_error(error, error_len, 0, "configuration has no stack entries", path);
    saw_invalid = true;
  }
  if (!saw_invalid && config->_has_self_ip != (config->_self_port_count > 0)) {
    if (strict) {
      config_error(error, error_len, 0, "incomplete self.ip/self.port mapping", path);
      saw_invalid = true;
    } else {
      config->_has_self_mapping = false;
    }
  }
  if (!saw_invalid) {
    config->_has_self_mapping = config->_has_self_ip && config->_self_port_count > 0;
  }
  if (saw_invalid) {
    delete config;
    return NULL;
  }
  return config;
}

bool UBSocketConfig::check_stack() const {
  JavaThread* jt = JavaThread::current();
  if (!jt->has_last_Java_frame()) { return false; }
  ResourceMark rm;
  RegisterMap reg_map(jt);
  javaVFrame* frame = jt->last_java_vframe(&reg_map);
  int depth = 0;
  while (frame != NULL) {
    Method* method = frame->method();
    for (StackEntry* item = _stack_head; item != NULL; item = item->next) {
      if (item->class_name->fast_compare(method->klass_name()) == 0 &&
          item->method_name->fast_compare(method->name()) == 0) {
        return true;
      }
    }
    frame = frame->java_sender();
    if (MaxJavaStackTraceDepth == ++depth) { break; }
  }
  return false;
}

bool UBSocketConfig::control_endpoint_for_data(const UBSocketEndpoint* data_ep,
                                               UBSocketEndpoint* control_ep) const {
  for (RemoteMappingEntry* item = _remote_head; item != NULL; item = item->next) {
    if (ub_socket_endpoint_equals(&item->public_data, data_ep)) {
      *control_ep = item->public_control;
      return true;
    }
  }
  return false;
}

bool UBSocketConfig::matches_local_service(const UBSocketEndpoint* local_ep) const {
  if (!_has_self_mapping || _self_local_ip == NULL) { return false; }
  if (!_self_local_ip_wildcard && !config_ip_equals(_self_local_ip, local_ep)) {
    return false;
  }
  for (SelfPortEntry* item = _self_port_head; item != NULL; item = item->next) {
    if (item->local_port == local_ep->port) { return true; }
  }
  return false;
}

bool UBSocketConfig::has_mapping_for_data(const UBSocketEndpoint* data_ep) const {
  if (!_has_self_mapping || _self_public_ip == NULL) { return false; }
  if (!config_ip_equals(_self_public_ip, data_ep)) { return false; }
  for (SelfPortEntry* item = _self_port_head; item != NULL; item = item->next) {
    if (item->public_port == data_ep->port) { return true; }
  }
  return false;
}

bool UBSocketConfig::matches_local_data(const UBSocketEndpoint* request_ep,
                                        const UBSocketEndpoint* session_ep) const {
  if (!has_mapping_for_data(request_ep)) {
    return ub_socket_endpoint_equals(request_ep, session_ep);
  }
  for (SelfPortEntry* item = _self_port_head; item != NULL; item = item->next) {
    if (item->public_port == request_ep->port) {
      return item->local_port == session_ep->port &&
             (_self_local_ip_wildcard || config_ip_equals(_self_local_ip, session_ep));
    }
  }
  return false;
}

static void config_print_endpoint(outputStream* out, const UBSocketEndpoint* endpoint,
                                  bool include_port) {
  char ip[INET6_ADDRSTRLEN];
  const char* value = endpoint->family == AF_INET
      ? inet_ntop(AF_INET, endpoint->addr, ip, sizeof(ip))
      : inet_ntop(AF_INET6, endpoint->addr, ip, sizeof(ip));
  if (endpoint->family == AF_INET6) { out->print("[%s]", value == NULL ? "<invalid>" : value); }
  else { out->print("%s", value == NULL ? "<invalid>" : value); }
  if (include_port) { out->print(":%u", endpoint->port); }
}

void UBSocketConfig::print_on(outputStream* out) const {
  out->print_cr("#stack");
  for (StackEntry* item = _stack_head; item != NULL; item = item->next) {
    out->print_cr("%s.%s", item->class_name->as_C_string(), item->method_name->as_C_string());
  }
  out->print_cr("#address");
  if (_has_self_ip && _self_public_ip != NULL && _self_local_ip != NULL) {
    out->print("self.ip=");
    config_print_endpoint(out, _self_public_ip, false);
    out->print(" -> ");
    if (_self_local_ip_wildcard) { out->print("*"); }
    else { config_print_endpoint(out, _self_local_ip, false); }
    out->cr();
  }
  for (SelfPortEntry* item = _self_port_head; item != NULL; item = item->next) {
    out->print_cr("self.port=%u -> %u", item->public_port, item->local_port);
  }
  for (RemoteMappingEntry* item = _remote_head; item != NULL; item = item->next) {
    out->print("remote.mapping=");
    config_print_endpoint(out, &item->public_data, true);
    out->print(" -> ");
    config_print_endpoint(out, &item->public_control, true);
    out->cr();
  }
}
