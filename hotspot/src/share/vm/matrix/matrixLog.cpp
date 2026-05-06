/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
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

#include "matrix/matrixLog.hpp"

#include <string.h>

#include "matrix/matrixUtils.hpp"
#include "memory/resourceArea.hpp"
#include "runtime/arguments.hpp"
#include "runtime/os.hpp"
#include "runtime/thread.hpp"
#include "utilities/globalDefinitions.hpp"

bool MatrixLog::_initialized = false;
int MatrixLog::_min_log_level = UB_LOG_ERROR;
char* MatrixLog::_default_log_path = NULL;
outputStream* MatrixLog::_default_log_file = NULL;
int MatrixLog::_log_levels[UB_FEATURE_COUNT] = {0};
char* MatrixLog::_log_paths[UB_FEATURE_COUNT] = {0};
outputStream* MatrixLog::_log_files[UB_FEATURE_COUNT] = {0};

static const char* ub_log_level_name(UBLogLevel level) {
  switch (level) {
    case UB_LOG_DEBUG:    return "DEBUG";
    case UB_LOG_INFO:     return "INFO";
    case UB_LOG_WARNING:  return "WARNING";
    case UB_LOG_ERROR:    return "ERROR";
    default:              return "UNKNOWN";
  }
}

static const char* ub_feature_name(UBFeature feature) {
  switch (feature) {
    case UB_SOCKET: return "socket";
    default:        return "unknown";
  }
}

static bool word_equals(const char* start, size_t len, const char* expected) {
  return strlen(expected) == len && strncmp(start, expected, len) == 0;
}

static bool ub_log_parse_feature(const char* start, size_t len,
                                 UBFeature* feature) {
  if (word_equals(start, len, "socket")) {
    *feature = UB_SOCKET;
    return true;
  }
  return false;
}

static bool ub_log_parse_level(const char* start, size_t len, int* level) {
  if (word_equals(start, len, "debug")) { *level = UB_LOG_DEBUG; return true; }
  if (word_equals(start, len, "info")) { *level = UB_LOG_INFO; return true; }
  if (word_equals(start, len, "warning")) { *level = UB_LOG_WARNING; return true; }
  if (word_equals(start, len, "error")) { *level = UB_LOG_ERROR; return true; }
  return false;
}

static void trim_token(const char** start, const char** end) {
  while (*start < *end && **start == ' ') { (*start)++; }
  while (*end > *start && *(*end - 1) == ' ') { (*end)--; }
}

static char* dup_token(const char* start, const char* end) {
  trim_token(&start, &end);
  size_t len = (size_t)(end - start);
  if (len == 0) { return NULL; }
  char pid_buf[16];
  int pid_len = jio_snprintf(pid_buf, sizeof(pid_buf), "%d", os::current_process_id());
  if (pid_len < 0) { return NULL; }

  size_t pid_pattern_count = 0;
  for (const char* p = start; p + 1 < end; p++) {
    if (p[0] == '%' && p[1] == 'p') {
      pid_pattern_count++;
      p++;
    }
  }

  size_t buf_len = len + pid_pattern_count * (size_t)pid_len + 1;
  char* value = NEW_C_HEAP_ARRAY(char, buf_len, mtInternal);
  if (!Arguments::copy_expand_pid(start, len, value, buf_len)) {
    FREE_C_HEAP_ARRAY(char, value, mtInternal);
    return NULL;
  }
  return value;
}

void MatrixLog::set_default_log_path(const char* start, const char* end) {
  char* path = dup_token(start, end);
  if (path != NULL) {
    _default_log_path = path;
  }
}

void MatrixLog::set_log_path(UBFeature feature, const char* start, const char* end) {
  char* path = dup_token(start, end);
  if (path != NULL) {
    _log_paths[feature] = path;
  }
}

static outputStream* open_log_file(const char* path) {
  if (path == NULL) { return NULL; }
  fileStream* log_file = new (ResourceObj::C_HEAP, mtInternal) fileStream(path, "a");
  if (log_file == NULL) { return NULL; }
  if (log_file->is_open()) { return log_file; }
  delete log_file;
  return NULL;
}

void MatrixLog::apply_selector(const char* token_start, const char* token_end) {
  trim_token(&token_start, &token_end);
  if (token_start == token_end) { return; }

  const char* equals = token_start;
  while (equals < token_end && *equals != '=') { equals++; }

  const char* feature_start = token_start;
  const char* feature_end = equals;
  trim_token(&feature_start, &feature_end);
  size_t feature_len = (size_t)(feature_end - feature_start);

  if (word_equals(feature_start, feature_len, "path")) {
    if (equals < token_end) {
      set_default_log_path(equals + 1, token_end);
    }
    return;
  }
  if (equals < token_end) {
    if (word_equals(feature_start, feature_len, "socket_path")) {
      set_log_path(UB_SOCKET, equals + 1, token_end);
      return;
    }
  }

  int level = UB_LOG_DEBUG;
  if (equals < token_end) {
    const char* level_start = equals + 1;
    const char* level_end = token_end;
    trim_token(&level_start, &level_end);
    size_t level_len = (size_t)(level_end - level_start);
    if (!ub_log_parse_level(level_start, level_len, &level)) {
      tty->print_cr("Ignore invalid UBLog level: %.*s",
                    (int)level_len, level_start);
      return;
    }
    _min_log_level = level < _min_log_level ? level : _min_log_level;
  }

  if (word_equals(feature_start, feature_len, "all")) {
    for (int i = 0; i < UB_FEATURE_COUNT; i++) {
      _log_levels[i] = level;
    }
    return;
  }

  UBFeature feature = UB_SOCKET;
  if (!ub_log_parse_feature(feature_start, feature_len, &feature)) {
    tty->print_cr("Ignore invalid UBLog selector: %.*s",
                  (int)feature_len, feature_start);
    return;
  }
  _log_levels[feature] = level;
}

void MatrixLog::init() {
  _default_log_file = tty;

  for (int i = 0; i < UB_FEATURE_COUNT; i++) {
    _log_levels[i] = UB_LOG_WARNING;
    _log_paths[i] = NULL;
    _log_files[i] = NULL;
  }

  const char* cursor = UBLog;
  while (!ub_option_blank(cursor)) {
    const char* token_start = cursor;
    while (*cursor != '\0' && *cursor != ',') { cursor++; }
    apply_selector(token_start, cursor);
    if (*cursor == ',') { cursor++; }
  }

  outputStream* default_file = open_log_file(_default_log_path);
  if (default_file != NULL) {
    _default_log_file = default_file;
  }
  for (int i = 0; i < UB_FEATURE_COUNT; i++) {
    outputStream* feature_file = open_log_file(_log_paths[i]);
    if (feature_file != NULL) {
      _log_files[i] = feature_file;
    }
  }

  _initialized = true;
}

bool MatrixLog::should_log(UBFeature feature, UBLogLevel level) {
  if (!_initialized) return false;
  if (feature < 0 || feature >= UB_FEATURE_COUNT) {
    return false;
  }
  return (int)level >= _log_levels[feature];
}

bool MatrixLog::enabled(UBFeature feature) {
  if (feature < 0 || feature >= UB_FEATURE_COUNT) { return false; }
  return _log_levels[feature] < UB_LOG_WARNING;
}

outputStream* MatrixLog::log_file(UBFeature feature) {
  if (feature >= 0 && feature < UB_FEATURE_COUNT && _log_files[feature] != NULL) {
    return _log_files[feature];
  }
  return _default_log_file == NULL ? tty : _default_log_file;
}

void MatrixLog::log(UBFeature feature, UBLogLevel level,
                    const char* format, ...) {
  char buf[32];
  const char* level_name = ub_log_level_name(level);
  outputStream* stream = log_file(feature);
  stream->print("{%s}", os::iso8601_time(buf, sizeof(buf)));
  stream->print("{%" PRId64 "}[%p][%s][%s] ",
                os::javaTimeNanos(), JavaThread::current(),
                ub_feature_name(feature), level_name);
  va_list ap;
  va_start(ap, format);
PRAGMA_DIAG_PUSH
PRAGMA_FORMAT_NONLITERAL_IGNORED_INTERNAL
  stream->vprint(format, ap);
PRAGMA_DIAG_POP
  va_end(ap);
}

void MatrixLog::flush() {
  if (_default_log_file != NULL && _default_log_file != tty) {
    _default_log_file->flush();
  }
  for (int i = 0; i < UB_FEATURE_COUNT; i++) {
    if (_log_files[i] != NULL && _log_files[i] != tty &&
        _log_files[i] != _default_log_file) {
      _log_files[i]->flush();
    }
  }
}
