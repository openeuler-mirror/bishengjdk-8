/*
 * Copyright (c) 2003, 2013, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_VM_MEMORY_FILEMAP_HPP
#define SHARE_VM_MEMORY_FILEMAP_HPP

#include "memory/metaspaceShared.hpp"
#include "memory/metaspace.hpp"
#include "runtime/os.hpp"
#include "utilities/align.hpp"

// Layout of the file:
//  header: dump of archive instance plus versioning info, datestamp, etc.
//   [magic # = 0xF00BABA2]
//  ... padding to align on page-boundary
//  read-write space from CompactingPermGenGen
//  read-only space from CompactingPermGenGen
//  misc data (block offset table, string table, symbols, dictionary, etc.)
//  tag(666)

#define CDS_ARCHIVE_MAGIC         0xf00baba2
#define CDS_DYNAMIC_ARCHIVE_MAGIC 0xf00baba8

static const int JVM_IDENT_MAX = 256;

class BitMap;
class Metaspace;

class SharedClassPathEntry VALUE_OBJ_CLASS_SPEC {
public:
  const char *_name;
  time_t _timestamp;          // jar timestamp,  0 if is directory
  long   _filesize;           // jar file size, -1 if is directory
  bool _sys_class;
  bool is_dir() {
    return _filesize == -1;
  }
  void set_sys_class(bool isSysClass) {
    _sys_class = isSysClass;
  }
};

class FileMapInfo : public CHeapObj<mtInternal> {
private:
  friend class ManifestStream;
  enum {
    _invalid_version = -1,
    _current_version = 3,
  };

  bool    _is_static;
  bool    _file_open;
  bool    _is_mapped;
  int     _fd;
  size_t  _file_offset;

private:
  static SharedClassPathEntry* _classpath_entry_table;
  static int                   _classpath_entry_table_size;
  static size_t                _classpath_entry_size;
  static bool                  _validating_classpath_entry_table;

  // FileMapHeader describes the shared space data in the file to be
  // mapped.  This structure gets written to a file.  It is not a class, so
  // that the compilers don't add any compiler-private data to it.

public:
  struct FileMapHeaderBase : public CHeapObj<mtClass> {
    virtual bool validate() = 0;
    virtual void populate(FileMapInfo* info, size_t alignment) = 0;
    // Use data() and data_size() to memcopy to/from the FileMapHeader. We need to
    // avoid read/writing the C++ vtable pointer.
    virtual size_t data_size() = 0;
  };
  struct FileMapHeader : FileMapHeaderBase {
    size_t data_size();
    char* data() {
      return ((char*)this) + sizeof(FileMapHeaderBase);
    }

    unsigned int _magic;                    // identify file type.
    int          _crc;                      // header crc checksum.
    int          _version;                  // (from enum, above.)
    size_t       _alignment;                // how shared archive should be aligned
    int          _obj_alignment;            // value of ObjectAlignmentInBytes
    bool         _is_default_jsa;           // indicates whether is the default jsa file

    struct space_info {
      int    _crc;           // crc checksum of the current space
      size_t _file_offset;   // sizeof(this) rounded to vm page size
      char*  _base;          // copy-on-write base address
      size_t _capacity;      // for validity checking
      size_t _used;          // for setting space top on read
      bool   _read_only;     // read only space?
      bool   _allow_exec;    // executable code in space?
    } _space[MetaspaceShared::n_regions];

    // The following fields are all sanity checks for whether this archive
    // will function correctly with this JVM and the bootclasspath it's
    // invoked with.
    char  _jvm_ident[JVM_IDENT_MAX];      // identifier for jvm

    // The _paths_misc_info is a variable-size structure that records "miscellaneous"
    // information during dumping. It is generated and validated by the
    // SharedPathsMiscInfo class. See SharedPathsMiscInfo.hpp and sharedClassUtil.hpp for
    // detailed description.
    //
    // The _paths_misc_info data is stored as a byte array in the archive file header,
    // immediately after the _header field. This information is used only when
    // checking the validity of the archive and is deallocated after the archive is loaded.
    //
    // Note that the _paths_misc_info does NOT include information for JAR files
    // that existed during dump time. Their information is stored in _classpath_entry_table.
    int _paths_misc_info_size;

    // The following is a table of all the class path entries that were used
    // during dumping. At run time, we require these files to exist and have the same
    // size/modification time, or else the archive will refuse to load.
    //
    // All of these entries must be JAR files. The dumping process would fail if a non-empty
    // directory was specified in the classpaths. If an empty directory was specified
    // it is checked by the _paths_misc_info as described above.
    //
    // FIXME -- if JAR files in the tail of the list were specified but not used during dumping,
    // they should be removed from this table, to save space and to avoid spurious
    // loading failures during runtime.
    int _classpath_entry_table_size;
    size_t _classpath_entry_size;
    SharedClassPathEntry* _classpath_entry_table;

    virtual bool validate();
    virtual void populate(FileMapInfo* info, size_t alignment);
    int crc() { return _crc; }
    int space_crc(int i) { return _space[i]._crc; }
    int compute_crc();
    unsigned int magic()                    const { return _magic; }
    const char* jvm_ident()                 const { return _jvm_ident; }
  };

  // Fixme
  struct DynamicArchiveHeader : FileMapHeader {
  private:
    int    _base_header_crc;
    int    _base_region_crc[MetaspaceShared::n_regions];
    char*  _requested_base_address;  // Archive relocation is not necessary if we map with this base address.
    size_t _ptrmap_size_in_bits;     // Size of pointer relocation bitmap
    size_t _base_archive_name_size;
    size_t _serialized_data_offset;  // Data accessed using {ReadClosure,WriteClosure}::serialize()

  public:
    size_t data_size();
    int base_header_crc() const { return _base_header_crc; }
    int base_region_crc(int i) const {
      return _base_region_crc[i];
    }

    void set_base_header_crc(int c) { _base_header_crc = c; }
    void set_base_region_crc(int i, int c) {
      _base_region_crc[i] = c;
    }

    void set_requested_base(char* b) {
      _requested_base_address = b;
    }
    size_t ptrmap_size_in_bits()             const { return _ptrmap_size_in_bits; }
    void set_ptrmap_size_in_bits(size_t s)         { _ptrmap_size_in_bits = s; }
    void set_base_archive_name_size(size_t s)      { _base_archive_name_size = s; }
    size_t base_archive_name_size()                { return _base_archive_name_size; }
    void set_as_offset(char* p, size_t *offset);
    char* from_mapped_offset(size_t offset)  const { return _requested_base_address + offset; }
    void set_serialized_data(char* p)              { set_as_offset(p, &_serialized_data_offset); }
    char* serialized_data()                  const { return from_mapped_offset(_serialized_data_offset); }

    virtual bool validate();
  };

  FileMapHeader * _header;

  const char* _full_path;
  const char* _appcds_file_lock_path;
  char* _paths_misc_info;

  static FileMapInfo* _current_info;
  static FileMapInfo* _dynamic_archive_info;

  static bool get_base_archive_name_from_header(const char* archive_name,
                                         int* size, char** base_archive_name);
  static bool check_archive(const char* archive_name, bool is_static);
  bool  init_from_file(int fd);
  void  align_file_position();
  bool  validate_header_impl();

public:
  FileMapInfo(bool is_static = true);
  ~FileMapInfo();

  static int current_version()        { return _current_version; }
  int    compute_header_crc();
  void   set_header_crc(int crc)      { _header->_crc = crc; }
  int    space_crc(int i)             { return _header->_space[i]._crc; }
  void   populate_header(size_t alignment);
  bool   validate_header();
  void   invalidate();
  int    crc()                        { return _header->_crc; }
  int    version()                    { return _header->_version; }
  size_t alignment()                  { return _header->_alignment; }
  size_t space_capacity(int i)        { return _header->_space[i]._capacity; }
  size_t used(int i)                  { return _header->_space[i]._used; }
  size_t used_aligned(int i)          { return align_up(used(i), (size_t)os::vm_allocation_granularity()); }
  char*  region_base(int i)           { return _header->_space[i]._base; }
  char*  region_end(int i)            { return region_base(i) + used_aligned(i); }
  struct FileMapHeader* header()      { return _header; }
  struct DynamicArchiveHeader* dynamic_header() {

    return (struct DynamicArchiveHeader*)header();
  }

  void set_header_base_archive_name_size(size_t size)      { dynamic_header()->set_base_archive_name_size(size); }

  static FileMapInfo* current_info() {
    CDS_ONLY(return _current_info;)
    NOT_CDS(return NULL;)
  }

  static FileMapInfo* dynamic_info() {
    CDS_ONLY(return _dynamic_archive_info;)
    NOT_CDS(return NULL;)
  }

  static void assert_mark(bool check);

  // File manipulation.
  bool  initialize() NOT_CDS_RETURN_(false);
  bool  open_for_read();
  void  open_for_write();
  void  write_header();
  void  write_dynamic_header();
  void  write_space(int i, Metaspace* space, bool read_only);
  void  write_region(int region, char* base, size_t size,
                     size_t capacity, bool read_only, bool allow_exec);
  char* write_bitmap_region(const BitMap* ptrmap);
  void  write_bytes(const void* buffer, int count);
  void  write_bytes_aligned(const void* buffer, int count);
  char* map_region(int i);
  void  unmap_region(int i);
  bool  verify_region_checksum(int i);
  void  close();
  bool  is_open()                                   { return _file_open; }
  bool  is_static()                           const { return _is_static; }
  bool  is_mapped()                           const { return _is_mapped; }
  bool  is_default_jsa()                      const { return _header->_is_default_jsa; }
  void  set_is_default_jsa(bool v)                  { _header->_is_default_jsa = v; }
  void  set_is_mapped(bool v)                       { _is_mapped = v; }
  ReservedSpace reserve_shared_memory();
  void set_requested_base(char* b)                  { dynamic_header()->set_requested_base(b); }
  char* serialized_data()                           { return dynamic_header()->serialized_data(); }
  // JVM/TI RedefineClasses() support:
  // Remap the shared readonly space to shared readwrite, private.
  bool  remap_shared_readonly_as_readwrite();

  // Errors.
  static void fail_stop(const char *msg, ...) ATTRIBUTE_PRINTF(1, 2);
  static void fail_continue(const char *msg, ...) ATTRIBUTE_PRINTF(1, 2);

  // Return true if given address is in the mapped shared space.
  bool is_in_shared_space(const void* p) NOT_CDS_RETURN_(false);
  void print_shared_spaces() NOT_CDS_RETURN;

  static size_t shared_spaces_size() {
    return align_size_up(SharedReadOnlySize + SharedReadWriteSize +
                         SharedMiscDataSize + SharedMiscCodeSize,
                         os::vm_allocation_granularity());
  }

  // Stop CDS sharing and unmap CDS regions.
  static void stop_sharing_and_unmap(const char* msg);

  static void allocate_classpath_entry_table();
  bool validate_classpath_entry_table();

  static SharedClassPathEntry* shared_classpath(int index) {
    char* p = (char*)_classpath_entry_table;
    p += _classpath_entry_size * index;
    return (SharedClassPathEntry*)p;
  }
  static const char* shared_classpath_name(int index) {
    return shared_classpath(index)->_name;
  }

  static int get_number_of_share_classpaths() {
    return _classpath_entry_table_size;
  }
};

#endif // SHARE_VM_MEMORY_FILEMAP_HPP
