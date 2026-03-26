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

#include "matrix/ubFile.hpp"

#include <unistd.h>

#include "classfile/symbolTable.hpp"
#include "matrix/matrixManager.hpp"
#include "matrix/ubFileMemPool.hpp"
#include "matrix/ubSocket.hpp"
#include "memory/resourceArea.hpp"
#include "runtime/thread.hpp"
#include "runtime/vframe.hpp"

MappedFilePathTable* UBFileGlobal::file_path_table = NULL;
MappedFileAddrTable* UBFileGlobal::file_addr_table = NULL;
MappedFileSizeTable* UBFileGlobal::file_size_table = NULL;
FileFallbackSet* UBFileGlobal::file_fallback_set = NULL;
AppFileTable* UBFileGlobal::app_file_table = NULL;
bool UBFileGlobal::_initialized = false;

const char* UB_FILE_PREFIX = "application_";

int UBFileInfoTable::open(const char* name, int oflags) {
  // Currently, _fake_fd_record is thread local int
  int fd = ++_fake_fd_record;
  guarantee(fd < INT_MAX, "fake fd overflow");
  void* head_blk_addr = NULL;

  Symbol* key = SymbolTable::new_symbol(name, JavaThread::current());
  UBFileInfo info(fd);
  info.name = key;

  void** find_addr = UBFileGlobal::file_addr_table->lookup(key);
  if (find_addr != NULL) {
    head_blk_addr = *find_addr;
    UBFileMemPool::flush_shared_memory(head_blk_addr);
    UBFileMemPool::get_used_size(head_blk_addr, &(info.length));
  } else if (oflags & O_CREAT) {
    if (UBFileGlobal::file_fallback_set->exist(key)) {
      UB_LOG("DEBUG", "UB open rejected, %s has fallbacked to IO\n", name);
      return -1;
    }
    int malloc_res = UBFileMemPool::malloc_remote_memory(name);
    if (malloc_res != 0) {
      UB_LOG("WARNING", "UB malloc failed %s, code %d, fallback to IO\n", name, malloc_res);
      return -1;
    }
    UBFileGlobal::app_file_table->record_file(name);
    head_blk_addr = UBFileMemPool::mmap_remote_memory(name);
    if (head_blk_addr == NULL) {
      UB_LOG("WARNING", "UB mmap failed %s, fallback to IO\n", name);
      return -1;
    }
    UBFileGlobal::file_addr_table->add(key, head_blk_addr);
  } else {
    UB_LOG("ERROR", "UB mmap cache miss %s(%p)\n", name, key);
    guarantee(false, name);
  }
  if (oflags & O_APPEND) {
    info.offset = info.length;
  }
  info.flags = oflags;
  info.head_blk = head_blk_addr;
  info.cur_blk = UBFileMemPool::seek_shared_memory(
      head_blk_addr, info.offset, &(info.cur_blk_size), &(info.cur_blk_off));
  add(fd, info);
  UB_LOG("DEBUG", "open %s(%p:%d) addr at %p len %ld cur blk %p %ld\n", name,
         key, fd, head_blk_addr, info.length, info.cur_blk, info.cur_blk_off);
  return fd;
}

void* UBFileInfoTable::pre_write(int fake_fd, long* nwrite, size_t len) {
  UBFileInfo* info = lookup(fake_fd);
  char msg[100];
  jio_snprintf(msg, sizeof(msg), "[%p] fd %d miss", JavaThread::current(),
               fake_fd);
  guarantee(info != NULL, msg);
  uintptr_t write_start = uintptr_t(info->cur_blk) + info->cur_blk_off;
  UB_LOG("DEBUG", "%s(%d): cur blk %p(%ld/%ld) write at %p size %ld\n",
         info->name->as_C_string(), fake_fd, info->cur_blk, info->cur_blk_off,
         info->cur_blk_size, (void*)write_start, len);
  // Current block is overflowed
  if (info->cur_blk_off + len >= info->cur_blk_size) {
    *nwrite = long(info->cur_blk_size - info->cur_blk_off);
    info->cur_blk = UBFileMemPool::seek_shared_memory(
        info->cur_blk, info->cur_blk_size, &(info->cur_blk_size),
        &(info->cur_blk_off));
    if (info->cur_blk == NULL) {
      UB_LOG("WARNING", "%s(%d) write at %lx size %ld, expand failed, try to fallback\n",
             info->name->as_C_string(), fake_fd, write_start, *nwrite);
      return NULL;
    }
    info->offset += *nwrite;
    info->length = info->offset > info->length ? info->offset : info->length;
    UB_LOG("DEBUG", "%s(%d) write len %ld at %lx, expand %p size %ld off %ld\n",
           info->name->as_C_string(), fake_fd, *nwrite, write_start,
           info->cur_blk, info->cur_blk_size, info->cur_blk_off);
    return (void*)write_start;
  }
  info->cur_blk_off += len;
  info->offset += len;
  info->length = info->offset > info->length ? info->offset : info->length;
  *nwrite = (long)len;
  return (void*)write_start;
}

void* UBFileInfoTable::pre_read(int fake_fd, long* nread, size_t len) {
  UBFileInfo* info = lookup(fake_fd);
  char msg[100];
  jio_snprintf(msg, sizeof(msg), "[%p] fd %d miss", JavaThread::current(),
               fake_fd);
  guarantee(info != NULL, msg);
  uintptr_t read_start = uintptr_t(info->cur_blk) + info->cur_blk_off;
  UB_LOG("DEBUG", "%s(%d):%p read at %p size %ld\n", info->name->as_C_string(),
         fake_fd, info->cur_blk, (void*)read_start, len);
  // Current block is overflowed
  // if return nread 0 (off + len = size), it will make a confusion with EOF
  if (info->cur_blk_off + len >= info->cur_blk_size) {
    *nread = long(info->cur_blk_size - info->cur_blk_off);
    info->offset += *nread;
    info->cur_blk = UBFileMemPool::seek_shared_memory(
        info->cur_blk, info->cur_blk_size, &(info->cur_blk_size),
        &(info->cur_blk_off));
    UB_LOG("DEBUG", "%s(%d)read len %ld at %lx, expand %p size %ld off %ld\n",
           info->name->as_C_string(), fake_fd, *nread, read_start,
           info->cur_blk, info->cur_blk_size, info->cur_blk_off);
    return (void*)read_start;
  }
  size_t read_size = info->length - info->offset;
  read_size = read_size > len ? len : read_size;
  info->cur_blk_off += read_size;
  info->offset += read_size;
  *nread = (long)read_size;
  return (void*)read_start;
}

int UBFileInfoTable::close(int fake_fd) {
  UBFileInfo* info = lookup(fake_fd);
  // FileInputStream.finalize could be mismatched
  if (info == NULL) {
    JavaThread* jt = JavaThread::current();
    if (!jt->has_last_Java_frame()) {
      UB_LOG("ERROR", "ub file %d miss in close\n", fake_fd);
      return 0;
    }
    ResourceMark rm;
    RegisterMap reg_map(jt);
    javaVFrame* jvf = jt->last_java_vframe(&reg_map);
    Method* method = jvf->method();
    UB_LOG("ERROR", "ub file %d miss in %s.%s\n", fake_fd,
           method->klass_name()->as_C_string(), method->name()->as_C_string());
    return 0;
  }
  UB_LOG("DEBUG", "close %s(%d) len %ld\n", info->name->as_C_string(), fake_fd,
         info->length);
  // Flush OP will update used length in block header
  UBFileMemPool::flush_shared_memory(info->head_blk, info->length);
  UBFileGlobal::file_size_table->add(info->name, info->length);
  remove(fake_fd);
  return 0;
}

size_t UBFileInfoTable::size(int fake_fd) {
  UBFileInfo* info = lookup(fake_fd);
  char msg[100];
  jio_snprintf(msg, sizeof(msg), "[%p] fd %d miss\n", JavaThread::current(),
               fake_fd);
  guarantee(info != NULL, msg);
  return info->length;
}

size_t UBFileInfoTable::seek(int fake_fd, long offset, int mode) {
  UBFileInfo* info = lookup(fake_fd);
  char msg[100];
  jio_snprintf(msg, sizeof(msg), "{%" PRId64 "}[%p] fd %d miss\n",
               os::javaTimeNanos(), JavaThread::current(), fake_fd);
  guarantee(info != NULL, msg);
  if (mode == SEEK_SET) {
    info->offset = offset;
  } else if (mode == SEEK_CUR) {
    info->offset += offset;
  } else if (mode == SEEK_END) {
    info->offset = info->length + offset;
  }
  info->cur_blk = UBFileMemPool::seek_shared_memory(
      info->head_blk, info->offset, &(info->cur_blk_size),
      &(info->cur_blk_off));
  UB_LOG("DEBUG", "seek %s(%d) offset %ld mode %d cur_blk %p\n",
         info->name->as_C_string(), fake_fd, offset, mode, info->cur_blk);
  return info->offset;
}

Symbol* UBFileInfoTable::shadow_file(int fake_fd, int& new_fd) {
  UBFileInfo* info = lookup(fake_fd);
  char msg[100];
  jio_snprintf(msg, sizeof(msg), "{%" PRId64 "}[%p] fd %d miss\n",
               os::javaTimeNanos(), JavaThread::current(), fake_fd);
  guarantee(info != NULL, msg);
  ResourceMark rm;
  new_fd = open64(info->name->as_C_string(), info->flags, S_IRWXU);
  UB_LOG("DEBUG", "create shadow file %d(%s) -> %d\n", fake_fd,
         info->name->as_C_string(), new_fd);
  return info->name;
}

static Symbol* get_app_id(const char* filepath) {
  const char* start = strstr(filepath, UB_FILE_PREFIX);
  if (start == NULL) {
    UB_LOG("ERROR", "filepath invalid %s\n", filepath);
    guarantee(false, filepath);
  }
  const char* end = strchr(start, '/');
  if (end == NULL) {
    end = start + strlen(start);
  }
  return SymbolTable::new_symbol(start, end - start, JavaThread::current());
}

void AppFileTable::record_file(const char* filepath) {
  Symbol* key = get_app_id(filepath);
  Symbol* value = SymbolTable::new_symbol(filepath, JavaThread::current());
  SymbolList** list_addr = lookup(key);
  SymbolList* list = NULL;
  if (list_addr == NULL) {
    list = new SymbolList();
    add(key, list);
  } else {
    list = *list_addr;
  }
  list->append(value);
}

int MatrixFileManager::open(const char* name, int oflags) {
  if (!UBFileGlobal::initialized()) return -1;
  // need to delete
  if (strstr(name, "temp_local") != NULL) return -1;
  if (strstr(name, "index") != NULL) return -1;

  Symbol* key = SymbolTable::new_symbol(name, JavaThread::current());
  UB_LOG("DEBUG", "open file %s(%p) oflags:%d\n", name, key, oflags);
  if (!(oflags & O_CREAT) && is_ub_file(name) == -1) {
    UB_LOG("WARNING", "%s is not exist in ub\n", name);
    return -1;
  }
  int fd = _file_info_table.open(name, oflags);
  if (fd != -1) UBFileGlobal::file_path_table->add(key, fd);
  return fd;
}

void* MatrixFileManager::pre_write(int fake_fd, long* nwrite, long len) {
  return _file_info_table.pre_write(fake_fd, nwrite, len);
}

void* MatrixFileManager::pre_read(int fake_fd, long* nread, long len) {
  return _file_info_table.pre_read(fake_fd, nread, len);
}

int MatrixFileManager::close(int fake_fd) {
  return _file_info_table.close(fake_fd);
}

size_t MatrixFileManager::size(int fake_fd) {
  return _file_info_table.size(fake_fd);
}

size_t MatrixFileManager::seek(int fake_fd, long offset, int mode) {
  return _file_info_table.seek(fake_fd, offset, mode);
}

size_t MatrixFileManager::size(const char* path) {
  Symbol* key = SymbolTable::new_symbol(path, JavaThread::current());
  size_t* size_addr = UBFileGlobal::file_size_table->lookup(key);
  size_t size = size_addr != NULL ? *size_addr : 0;
  UB_LOG("DEBUG", "get %s(%p) file size %ld\n", path, key, size);
  return size;
}

int MatrixFileManager::is_ub_file(const char* path) {
  if (!UBFileGlobal::initialized()) return -1;
  if (strstr(path, UB_FILE_PREFIX) == NULL) return -1;
  Symbol* key = SymbolTable::new_symbol(path, JavaThread::current());
  int* fd_addr = UBFileGlobal::file_path_table->lookup(key);
  if (fd_addr != NULL) return *fd_addr;

  bool remote_exist = false;
  int err_code = UBFileMemPool::remote_name_exist(path, &remote_exist);
  if (err_code != 0) {
    UB_LOG("ERROR", "ub_name_exist %s error code %d\n", path, err_code);
    return -1;
  }
  if (!remote_exist) return -1;
  // Load remote ub file
  void* head_blk_addr = UBFileMemPool::mmap_remote_memory(path);
  UB_LOG("DEBUG", "load remote file %s(%p) at %p\n", path, key, head_blk_addr);
  UBFileGlobal::file_addr_table->add(key, head_blk_addr);
  UBFileGlobal::file_path_table->add(key, 0);
  return 0;
}

bool MatrixFileManager::is_ub_addr(void* addr) {
  bool res = false;
  if (!UBFileGlobal::initialized()) return res;
  int err_code = UBFileMemPool::shared_addr_exist(addr, &res);
  if (err_code != 0) {
    UB_LOG("ERROR", "ub_addr_exist %p error code %d\n", addr, err_code);
    return res;
  }
  UB_LOG("DEBUG", "ub addr %p exist %d\n", addr, res);
  return res;
}

bool MatrixFileManager::remove(const char* path) {
  if (!UBFileGlobal::initialized()) return false;
  Symbol* key = SymbolTable::new_symbol(path, JavaThread::current());
  void** find_addr = UBFileGlobal::file_addr_table->lookup(key);
  if (find_addr == NULL) {
    UB_LOG("WARNING", "file %s(%p) not exist", path, key);
    return true;
  }
  void* mapping_addr = *find_addr;
  UB_LOG("DEBUG", "remove file %s(%p)\n", path, mapping_addr);
  UBFileMemPool::munmap_shared_memory(mapping_addr);
  UBFileMemPool::free_remote_memory(path);
  UBFileGlobal::file_path_table->remove(key);
  UBFileGlobal::file_addr_table->remove(key);
  UBFileGlobal::file_size_table->remove(key);
  return true;
}

bool MatrixFileManager::remove_dir(const char* path) {
  if (!UBFileGlobal::initialized()) return false;
  Symbol* key = get_app_id(path);
  SymbolList** list_addr = UBFileGlobal::app_file_table->lookup(key);
  if (list_addr == NULL) {
    UB_LOG("WARNING", "dir %s key %p(%s) not exist\n", path, key,
           key->as_C_string());
    return false;
  }
  if (PrintUBLog) {
    size_t mem_used;
    size_t mem_alloc;
    size_t mem_total;
    UBFileMemPool::total_memory_info(&mem_used, &mem_alloc, &mem_total);
    UB_LOG("DEBUG", "current mem info: used %ld alloc %ld total %ld\n",
           mem_used, mem_alloc, mem_total);
  }
  SymbolList* list = *list_addr;
  UB_LOG("DEBUG", "remove dir %s: key %p(%s) size %d\n", path, key,
         key->as_C_string(), list->size());
  list->begin_iteration();
  Symbol* filename = list->next();
  while (filename != NULL) {
    remove(filename->as_C_string());
    UBFileGlobal::file_fallback_set->remove(filename);
    filename = list->next();
  }
  list->clear_list();
  UBFileGlobal::app_file_table->remove(key);
  return true;
}

bool MatrixFileManager::rename(const char* from, const char* to) {
  if (!UBFileGlobal::initialized()) return false;
  int res = UBFileMemPool::rename_remote_memory(from, to);
  if (res != 0) return false;

  Symbol* old_key = SymbolTable::new_symbol(from, JavaThread::current());
  int* fd_addr = UBFileGlobal::file_path_table->lookup(old_key);
  void** mapping_addr = UBFileGlobal::file_addr_table->lookup(old_key);
  size_t* size_addr = UBFileGlobal::file_size_table->lookup(old_key);
  guarantee(fd_addr != NULL && mapping_addr != NULL && size_addr != NULL, from);
  guarantee(*fd_addr >= _file_info_table.fd_limit(), "invalid fake fd");
  UBFileGlobal::file_path_table->remove(old_key);
  UBFileGlobal::file_addr_table->remove(old_key);
  UBFileGlobal::file_size_table->remove(old_key);
  Symbol* new_key = SymbolTable::new_symbol(to, JavaThread::current());
  UBFileGlobal::file_path_table->add(new_key, *fd_addr);
  UBFileGlobal::file_addr_table->add(new_key, *mapping_addr);
  UBFileGlobal::file_size_table->add(new_key, *size_addr);

  UB_LOG("DEBUG", "rename %s -> %s(%d) at %p size %ld\n", from, to, *fd_addr,
         *mapping_addr, *size_addr);

  Symbol* app_id = get_app_id(from);
  SymbolList** list_addr = UBFileGlobal::app_file_table->lookup(app_id);
  guarantee(list_addr != NULL, from);
  SymbolList* list = *list_addr;
  bool update_success = list->update(old_key, new_key);
  if (!update_success) {
    UB_LOG("WARNING", "update file list %p failed, %s -> %s\n", list, from, to);
  }

  return true;
}

void* MatrixFileManager::transfer(int dst, int src, long offset, long count, jlong* ntransfer) {
  int fd_limit = _file_info_table.fd_limit();
  if (dst < fd_limit && src < fd_limit && !UseUBSocket) {
    *ntransfer = 0;
    return NULL;
  }
  long copy_size;
  void* socket_addr = NULL;
  if (src >= fd_limit) {
    UBFileInfo* src_info = _file_info_table.lookup(src);
    char msg[100];
    jio_snprintf(msg, sizeof(msg), "[%p] fd %d miss", JavaThread::current(),
                 src);
    guarantee(src_info != NULL, msg);

    copy_size = src_info->length - offset;
    copy_size = copy_size > count ? count : copy_size;
    if (copy_size != count) {
      UB_LOG("WARNING", "transfer %d -> %d, offset %ld count %ld, but copysize %ld\n", src,
             dst, offset, count, copy_size);
    }
    _file_info_table.seek(src, offset, SEEK_SET);
    void* buffer = malloc(copy_size);
    long nread = 0;
    while (nread < copy_size) {
      long read_size = 0;
      void* read_start = _file_info_table.pre_read(src, &read_size, copy_size - nread);
      uintptr_t write_start = (uintptr_t)buffer + nread;
      memcpy((void*)write_start, read_start, read_size);
      nread += read_size;
    }
    guarantee(nread == copy_size, "must be");
    long nwrite = 0;
    if (dst >= fd_limit) {
      while (nwrite < copy_size) {
        long write_size = 0;
        void* write_start = _file_info_table.pre_write(dst, &write_size, copy_size - nwrite);
        uintptr_t read_start = (uintptr_t)buffer + nwrite;
        memcpy(write_start, (void*)read_start, write_size);
        nwrite += write_size;
      }
      guarantee(nwrite == copy_size, "must be");
      UBFileInfo* dst_info = _file_info_table.lookup(dst);
      guarantee(dst_info != NULL, "must be");
      UB_LOG("DEBUG",
             "transfer %d(%s) -> %d(%s), offset %ld count %ld, success %ld\n",
             src, src_info->name->as_C_string(), dst,
             dst_info->name->as_C_string(), offset, count, nwrite);
    } else {
      struct stat stat_buf;
      int success = fstat(dst, &stat_buf);
      if (UseUBSocket && success != -1 && S_ISSOCK(stat_buf.st_mode)) {
        long socket_nwrite = UBSocketManager::write_data(buffer, dst, copy_size);
        guarantee(socket_nwrite == copy_size, "must be");
        UB_LOG("DEBUG", "transfer %d(%s) -> %d, offset %ld count %ld\n", src,
               src_info->name->as_C_string(), dst, offset, count);
      } else {
        while (nwrite < copy_size) {
          uintptr_t write_start = (uintptr_t)buffer + nwrite;
          long write_size = write(dst, (void*)write_start, copy_size - nwrite);
          if (write_size == -1) {
            continue;  // errno == EINTR
          }
          nwrite += write_size;
        }
        UB_LOG("DEBUG", "transfer %d(%s) -> %d, offset %ld count %ld, success %d\n", src,
               src_info->name->as_C_string(), dst, offset, count, nwrite);
        char err_msg[100];
        jio_snprintf(err_msg, sizeof(err_msg), "[%p] fd %d->%d, nwrite(%ld) != copy_size(%ld)",
                     JavaThread::current(), src, dst, nwrite, copy_size);
        guarantee(nwrite == copy_size, err_msg);
      }
    }
    free(buffer);
  } else if (dst >= fd_limit) {
    struct stat fileStat;
    fstat(src, &fileStat);
    copy_size = fileStat.st_size < count ? fileStat.st_size : count;
    long nwrite = 0, nread = 0;
    while (nwrite < copy_size) {
      long write_size;
      void* dst_start =
          _file_info_table.pre_write(dst, &write_size, copy_size - nwrite);
      nread += pread64(src, dst_start, copy_size - nread, offset + nwrite);
      nwrite += write_size;
    }
    UB_LOG("DEBUG",
           "transfer %d -> %d, offset %ld count %ld, success %ld %ld\n", src,
           dst, offset, count, nwrite, nread);
    guarantee(nread == copy_size, "must be");
  } else if (UseUBSocket) {
    struct stat stat_buf;
    int success = fstat(dst, &stat_buf);
    if (success == -1 || !S_ISSOCK(stat_buf.st_mode)) {
      *ntransfer = 0;
      return NULL;
    }
    struct stat fileStat;
    fstat(src, &fileStat);
    copy_size = fileStat.st_size < count ? fileStat.st_size : count;
    long nread = 0;
    long socket_offset, socket_size;
    socket_addr = UBSocketManager::get_free_memory(copy_size, &socket_offset, &socket_size);
    long write_size = socket_size > copy_size ? copy_size : socket_size;
    while (nread < write_size) {
      uintptr_t dst_start = (uintptr_t)socket_addr + nread;
      nread += pread64(src, (void*)dst_start, write_size - nread, offset + nread);
    }
    UBSocketManager::send_msg(dst, socket_addr, socket_offset, write_size);
    if (copy_size > write_size) {
      socket_addr = UBSocketManager::get_free_memory(
          copy_size - write_size, &socket_offset, &socket_size);
      guarantee(socket_size >= (copy_size - write_size), "must be");
      while (nread < copy_size) {
        uintptr_t dst_start = (uintptr_t)socket_addr + nread;
        nread += pread64(src, (void*)dst_start, copy_size - nread, offset + nread);
      }
    }
    UB_LOG("DEBUG", "transfer %d -> %d, offset %ld count %ld, success %ld\n",
           src, dst, offset, count, nread);
    guarantee(nread == copy_size, "must be");
  }
  *ntransfer = copy_size;
  return socket_addr;
}

int MatrixFileManager::fallback(int fake_fd) {
  size_t size = _file_info_table.size(fake_fd);
  int new_fd;
  Symbol* filename = _file_info_table.shadow_file(fake_fd, new_fd);
  long* copy_size;
  transfer(new_fd, fake_fd, 0, size, copy_size);
  UB_LOG("DEBUG", "fallback %d -> %d, size %ld\n", fake_fd, new_fd, size);
  close(fake_fd);
  ResourceMark rm;
  remove(filename->as_C_string());
  UBFileGlobal::file_fallback_set->add(filename);
  return new_fd;
}

void UBFileGlobal::init() {
  if (!UseUBFile) return;
  bool is_success = UBFileMemPool::init();
  if (!is_success) return;

  file_path_table = new MappedFilePathTable();
  file_addr_table = new MappedFileAddrTable();
  file_size_table = new MappedFileSizeTable();
  file_fallback_set = new FileFallbackSet();
  app_file_table = new AppFileTable();

  _initialized = true;
}

void UBFileGlobal::before_exit() {
  if (!UseUBFile || !_initialized) return;
  if (PrintUBLog) {
    size_t mem_used;
    size_t mem_alloc;
    size_t mem_total;
    UBFileMemPool::total_memory_info(&mem_used, &mem_alloc, &mem_total);
    UB_LOG("DEBUG", "current mem info: used %ld alloc %ld total %ld\n",
           mem_used, mem_alloc, mem_total);
  }
  UBFileMemPool::release();
}
