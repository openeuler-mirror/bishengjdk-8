/*
 * Copyright (c) 2025, Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2019 Alibaba Group Holding Limited. All rights reserved.
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

#ifndef SHARED_VM_JPROFILECACHE_JITPROFILERECORD_HPP
#define SHARED_VM_JPROFILECACHE_JITPROFILERECORD_HPP

#include "utilities/hashtable.hpp"
#include "oops/method.hpp"
#include "oops/methodData.hpp"
#include "utilities/linkedlist.hpp"
#include "utilities/growableArray.hpp"

class ProfileData;
class ArgInfoData;

class JitProfileRecorderEntry : public HashtableEntry<Method*, mtInternal> {
public:
    JitProfileRecorderEntry() { }
    virtual ~JitProfileRecorderEntry() { }

    void init() {
        _bci = InvocationEntryBci;
    }

    void set_bci(int bci) { _bci = bci; }
    int  bci()            { return _bci; }

    void set_order(int order) { _order = order; }
    int  order()              { return _order; }

    JitProfileRecorderEntry* next() {
        return (JitProfileRecorderEntry*)HashtableEntry<Method*, mtInternal>::next();
  }

private:
    int    _bci;
    int    _order;
};

class JitProfileRecordDictionary : public Hashtable<Method*, mtInternal> {
    friend class VMStructs;
    friend class JitProfileCache;
public:
    JitProfileRecordDictionary(unsigned int size);
    virtual ~JitProfileRecordDictionary();

    JitProfileRecorderEntry* add_method(unsigned int method_hash, Method* method, int bci);

    JitProfileRecorderEntry* find_entry(unsigned int hash, Method* method);

    void free_entry(JitProfileRecorderEntry* entry);

    unsigned int count() { return _count; }

    void print();

    JitProfileRecorderEntry* bucket(int i) {
        return (JitProfileRecorderEntry*)Hashtable<Method*, mtInternal>::bucket(i);
    }

private:
    unsigned int _count;
    JitProfileRecorderEntry* new_entry(unsigned int hash, Method* method);
};

class ClassSymbolEntry {
public:
    ClassSymbolEntry(Symbol* class_name, Symbol* class_loader_name, Symbol* path, bool clinit_succeeded = false)
        : _class_name(class_name),
        _class_loader_name(class_loader_name),
        _class_path(path),
        _clinit_succeeded(clinit_succeeded) {
        retain(_class_name);
        retain(_class_loader_name);
        retain(_class_path);
    }

    ClassSymbolEntry()
        : _class_name(NULL),
        _class_loader_name(NULL),
        _class_path(NULL),
        _clinit_succeeded(false) {
    }

    ClassSymbolEntry(const ClassSymbolEntry& rhs)
        : _class_name(rhs._class_name),
        _class_loader_name(rhs._class_loader_name),
        _class_path(rhs._class_path),
        _clinit_succeeded(rhs._clinit_succeeded) {
        retain(_class_name);
        retain(_class_loader_name);
        retain(_class_path);
    }

    ClassSymbolEntry& operator=(const ClassSymbolEntry& rhs) {
        if (this != &rhs) {
            Symbol* new_class_name = rhs._class_name;
            Symbol* new_loader_name = rhs._class_loader_name;
            Symbol* new_class_path = rhs._class_path;

            retain(new_class_name);
            retain(new_loader_name);
            retain(new_class_path);

            release(_class_name);
            release(_class_loader_name);
            release(_class_path);

            _class_name = new_class_name;
            _class_loader_name = new_loader_name;
            _class_path = new_class_path;
            _clinit_succeeded = rhs._clinit_succeeded;
        }
        return *this;
    }

    ~ClassSymbolEntry() {
        release(_class_name);
        release(_class_loader_name);
        release(_class_path);
    }

    Symbol* class_name() const { return _class_name; }
    Symbol* class_loader_name() const { return _class_loader_name; }
    Symbol* path() const { return _class_path; }
    bool clinit_succeeded() const { return _clinit_succeeded; }
    void set_clinit_succeeded(bool value) { _clinit_succeeded = value; }

    bool equals(const ClassSymbolEntry& rhs) const {
        return _class_name == rhs._class_name;
    }

private:
    static void retain(Symbol* sym) {
        if (sym != NULL) {
            sym->increment_refcount();
        }
    }

    static void release(Symbol* sym) {
        if (sym != NULL) {
            sym->decrement_refcount();
        }
    }

    Symbol* _class_name;
    Symbol* _class_loader_name;
    Symbol* _class_path;
    bool _clinit_succeeded;
};

#define KNUTH_HASH_MULTIPLIER  2654435761UL
#define ADDR_CHANGE_NUMBER 3
#define JITPROFILECACHE_VERSION_V1 0x1
#define JITPROFILECACHE_VERSION_V2 0x2
#define JITPROFILECACHE_VERSION  JITPROFILECACHE_VERSION_V2

class JitProfileRecorder : public CHeapObj<mtInternal> {
public:
    enum RecorderState {
        IS_OK = 0,
        IS_ERR = 1,
        NOT_INIT = 2
    };
public:
    JitProfileRecorder();
    virtual ~JitProfileRecorder();

    void init();

    unsigned int version() { return JITPROFILECACHE_VERSION; }

    int class_init_count()                   { return _class_init_order_num + 1; }

    address current_init_order_addr() { return (address)&_class_init_order_num;}

    unsigned int is_flushed()                { return _flushed; }
    void         set_flushed(bool value)  { _flushed = value; }

    const char*  logfile_name()                      { return _record_file_name; }

    unsigned int recorded_count() { return _profile_record_dict->count(); }
    JitProfileRecordDictionary* dict() { return _profile_record_dict; }

    void         set_logfile_name(const char* name);

    bool is_valid() { return _recorder_state == IS_OK;}

    LinkedListImpl<ClassSymbolEntry>*
    class_init_list()                    { return _class_init_list; }

    void add_method(Method* method, int method_bci);

    void flush_record();

    int assign_class_init_order(InstanceKlass* klass);
    void mark_class_init_result(int init_order, bool success);

    unsigned int compute_hash(Method* method) {
        uint64_t m_addr = (uint64_t)method;
        return (m_addr >> ADDR_CHANGE_NUMBER) * KNUTH_HASH_MULTIPLIER; // Knuth multiply hash
    }

    static int compute_crc32(randomAccessFileStream* fileStream);

    static const char* auto_jpcfile_name();
    static const char* auto_temp_jpcfile_name();
    static void set_jpcfile_filepointer(FILE* file);
    static bool is_recordable_data(ProfileData* dp);
    static ArgInfoData* get_ArgInfoData(MethodData* mdo);

private:
    int                                          _max_symbol_length;
    unsigned int                                 _pos;
    volatile int                                 _class_init_order_num;
    volatile bool                                _flushed;
    const char*                                  _record_file_name;
    static const char*                           _auto_jpcfile_name;
    static const char*                           _auto_temp_jpcfile_name;
    static FILE*                                 _auto_jpcfile_filepointer;

    randomAccessFileStream*                      _profilelog;
    RecorderState                                _recorder_state;
    LinkedListImpl<ClassSymbolEntry>*            _class_init_list;
    LinkedListNode<ClassSymbolEntry>*            _init_list_tail_node;
    GrowableArray<LinkedListNode<ClassSymbolEntry>*>* _class_init_nodes;
    JitProfileRecordDictionary*                     _profile_record_dict;

private:
  void write_u1(u1 value);
  void write_u4(u4 value);
  void write_data_layout(ProfileData* value);

  void write_profilecache_header();
  void write_inited_class();
  void write_profilecache_record(Method* method, int bci, int order);
  void record_class_info(InstanceKlass* klass);
  void record_method_info(Method* method, ConstMethod* const_method, int bci);
  void write_profilecache_footer();
  void write_method_profiledata(MethodData* mdo);

  void write_string(const char* src, size_t len);
  void overwrite_u4(u4 value, unsigned int offset);

  void update_max_symbol_length(int len);
};

#endif // SHARED_VM_JPROFILECACHE_JITPROFILERECORD_HPP
