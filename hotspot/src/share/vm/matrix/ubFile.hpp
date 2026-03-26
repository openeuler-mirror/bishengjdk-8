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

#ifndef SHARE_VM_MATRIX_UBFILE_HPP
#define SHARE_VM_MATRIX_UBFILE_HPP

#include <sys/resource.h>

#include "matrix/matrixUtils.hpp"
#include "matrix/ubFileUtils.hpp"

class UBFileInfo : public CHeapObj<mtInternal> {
 public:
  int fake_fd;
  size_t length;  // total file len
  size_t offset;  // total file off
  int flags;      // open flags
  Symbol* name;

  void* head_blk;  // first block data addr
  void* cur_blk;   // current block data addr
  size_t cur_blk_off;
  size_t cur_blk_size;
  explicit UBFileInfo(int fd)
      : fake_fd(fd),
        length(0),
        offset(0),
        name(NULL),
        head_blk(NULL),
        cur_blk(NULL),
        cur_blk_off(0),
        cur_blk_size(0) {}
};

class UBFileInfoTable : public UBFileHashTable<int, UBFileInfo, mtInternal> {
 public:
  UBFileInfoTable() : UBFileHashTable() {
    struct rlimit rlp;
    int status = getrlimit(RLIMIT_NOFILE, &rlp);
    if (rlp.rlim_max < 0 || rlp.rlim_max >= INT_MAX) {
      guarantee(false, "TODO: more check handle");
    } else {
      this->_fake_fd_record = rlp.rlim_max;
      this->_fd_limit = rlp.rlim_max;
    }
  }

  int fd_limit() { return _fd_limit; }

  int open(const char* name, int oflags);
  void* pre_write(int fake_fd, long* nwrite, size_t len);
  void* pre_read(int fake_fd, long* nread, size_t len);
  int close(int fake_fd);
  size_t size(int fake_fd);
  size_t seek(int fake_fd, long offset, int mode);

  Symbol* shadow_file(int fake_fd, int& new_fd);

 private:
  int _fake_fd_record;
  int _fd_limit;
};

class MatrixFileManager : public CHeapObj<mtInternal> {
 public:
  int open(const char* name, int oflags);
  int close(int fake_fd);
  void* pre_write(int fake_fd, long* nwrite, long len);
  void* pre_read(int fake_fd, long* nread, long len);
  size_t size(int fake_fd);
  size_t size(const char* name);
  size_t seek(int fake_fd, long offset, int mode);
  bool remove(const char* name);
  bool remove_dir(const char* path);
  bool rename(const char* from, const char* to);
  void* transfer(int dst, int src, long offset, long count, jlong* ntransfer);
  int is_ub_file(const char* path);
  bool is_ub_addr(void* addr);
  int fallback(int fake_fd);

 private:
  UBFileInfoTable _file_info_table;
};

class MappedFilePathTable : public PtrTable<Symbol*, int, mtInternal> {
 public:
  MappedFilePathTable() : PtrTable(-1) {}
};

class MappedFileAddrTable : public PtrTable<Symbol*, void*, mtInternal> {
 public:
  MappedFileAddrTable() : PtrTable(NULL) {}
};

class MappedFileSizeTable : public PtrTable<Symbol*, size_t, mtInternal> {
 public:
  MappedFileSizeTable() : PtrTable(0) {}
};

class FileFallbackSet : public PtrHashSet<Symbol*, mtInternal> {};

class SymbolList : public PtrList<Symbol*> {
 public:
  void clear_list() { clear(_clear_item); }

 private:
  static void _clear_item(Symbol*) {}
};

class AppFileTable : public PtrTable<Symbol*, SymbolList*, mtInternal> {
 public:
  AppFileTable() : PtrTable(NULL) {}
  void record_file(const char* filepath);
};

class UBFileGlobal : public AllStatic {
 public:
  static MappedFilePathTable* file_path_table;
  static MappedFileAddrTable* file_addr_table;
  static MappedFileSizeTable* file_size_table;
  static FileFallbackSet* file_fallback_set;
  static AppFileTable* app_file_table;

  static bool initialized() { return _initialized; }
  static void init();
  static void before_exit();

 private:
  static bool _initialized;
};

#endif  // SHARE_VM_MATRIX_UBFILE_HPP