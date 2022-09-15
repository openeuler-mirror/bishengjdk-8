/*
 * Copyright (c) 2014, 2016, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_VM_CLASSFILE_SYSTEMDICTIONARYSHARED_HPP
#define SHARE_VM_CLASSFILE_SYSTEMDICTIONARYSHARED_HPP

#include "classfile/dictionary.hpp"
#include "classfile/systemDictionary.hpp"
#include "verifier.hpp"

/*===============================================================================

    Handling of the classes in the AppCDS archive

    To ensure safety and to simplify the implementation, archived classes are
    "segregated" into 2 types. The following rules describe how they
    are stored and looked up.

[1] Category of archived classes

    There are 2 disjoint groups of classes stored in the AppCDS archive:

    BUILTIN:              These classes may be defined ONLY by the BOOT/PLATFORM/APP
                          loaders.

    UNREGISTERED:         These classes may be defined ONLY by a ClassLoader
                          instance that's not listed above (using fingerprint matching)

[2] How classes from different categories are specified in the classlist:

    Starting from JDK9, each class in the classlist may be specified with
    these keywords: "id", "super", "interfaces", "loader" and "source".


    BUILTIN               Only the "id" keyword may be (optionally) specified. All other
                          keywords are forbidden.

                          The named class is looked up from the jimage and from
                          Xbootclasspath/a and CLASSPATH.

    UNREGISTERED:         The "id", "super", and "source" keywords must all be
                          specified.

                          The "interfaces" keyword must be specified if the class implements
                          one or more local interfaces. The "interfaces" keyword must not be
                          specified if the class does not implement local interfaces.

                          The named class is looked up from the location specified in the
                          "source" keyword.

    Example classlist:

    # BUILTIN
    java/lang/Object id: 0
    java/lang/Cloneable id: 1
    java/lang/String

    # UNREGISTERED
    Bar id: 3 super: 0 interfaces: 1 source: /foo.jar


[3] Identifying the category of archived classes

    BUILTIN:              (C->shared_classpath_index() >= 0)
    UNREGISTERED:         (C->shared_classpath_index() == UNREGISTERED_INDEX (-9999))

[4] Lookup of archived classes at run time:

    (a) BUILTIN loaders:

        search _builtin_dictionary

    (b) UNREGISTERED loaders:

        search _unregistered_dictionary for an entry that matches the
        (name, clsfile_len, clsfile_crc32).

===============================================================================*/
#define UNREGISTERED_INDEX -9999

class DumpTimeSharedClassInfo;
class RunTimeSharedClassInfo;
class RunTimeSharedDictionary;

class SystemDictionaryShared: public SystemDictionary {
private:
  static bool _dump_in_progress;
  DEBUG_ONLY(static bool _no_class_loading_should_happen;)

public:
  static void initialize(TRAPS) {}
  static instanceKlassHandle find_or_load_shared_class(Symbol* class_name,
                                                       Handle class_loader,
                                                       TRAPS) {
    if (UseSharedSpaces) {
      instanceKlassHandle ik = load_shared_class(class_name, class_loader, CHECK_NULL);
      if (!ik.is_null()) {
        instanceKlassHandle nh = instanceKlassHandle(); // null Handle
        ik = find_or_define_instance_class(class_name, class_loader, ik, CHECK_(nh));
      }
      return ik;
    }
    return instanceKlassHandle();
  }
  static void roots_oops_do(OopClosure* blk) {}
  static void oops_do(OopClosure* f) {}

  static bool is_sharing_possible(ClassLoaderData* loader_data) {
    oop class_loader = loader_data->class_loader();
    return (class_loader == NULL ||
            (UseAppCDS && (SystemDictionary::is_app_class_loader(class_loader) ||
                           SystemDictionary::is_ext_class_loader(class_loader)))
            );
  }

  static size_t dictionary_entry_size() {
    return sizeof(DictionaryEntry);
  }

  static void init_shared_dictionary_entry(Klass* k, DictionaryEntry* entry) {}

  static void init_dumptime_info(InstanceKlass* k) NOT_CDS_RETURN;
  static void remove_dumptime_info(InstanceKlass* k) NOT_CDS_RETURN;

  static void start_dumping();

  static DumpTimeSharedClassInfo* find_or_allocate_info_for(InstanceKlass* k);

  static DumpTimeSharedClassInfo* find_or_allocate_info_for_locked(InstanceKlass* k);

  static bool empty_dumptime_table();

  static void check_excluded_classes();

  static bool check_for_exclusion(InstanceKlass* k, DumpTimeSharedClassInfo* info);

  static bool has_been_redefined(InstanceKlass* k);

  static bool check_for_exclusion_impl(InstanceKlass* k);

  static bool warn_excluded(InstanceKlass* k, const char* reason);

  static bool is_jfr_event_class(InstanceKlass *k);

  static bool has_class_failed_verification(InstanceKlass* ik);

  static bool is_builtin(InstanceKlass* k) {
    return (k->shared_classpath_index() != UNREGISTERED_INDEX);
  }

  static void dumptime_classes_do(class MetaspaceClosure* it);

  static void replace_klass_in_constantPool();

  static bool is_excluded_class(InstanceKlass* k);
  // The (non-application) CDS implementation supports only classes in the boot
  // class loader, which ensures that the verification dependencies are the same
  // during archive creation time and runtime. Thus we can do the dependency checks
  // entirely during archive creation time.
  static void add_verification_dependency(Klass* k, Symbol* accessor_clsname,
                                          Symbol* target_clsname) {}
  static void finalize_verification_dependencies() {}
  static void set_class_has_failed_verification(InstanceKlass* ik);
  static bool check_verification_dependencies(Klass* k, Handle class_loader,
                                              Handle protection_domain,
                                              char** message_buffer, TRAPS) {
    if (EnableSplitVerifierForAppCDS) {
      ClassVerifier split_verifier(k, THREAD);
      split_verifier.verify_class(THREAD);
      if (HAS_PENDING_EXCEPTION) {
        return false; // use the existing exception
      }
    }
    return true;
  }
  static size_t estimate_size_for_archive();
  static void write_to_archive();
  static void write_dictionary(RunTimeSharedDictionary* dictionary, bool is_builtin);
  static void serialize_dictionary_headers(class SerializeClosure* soc);
  static unsigned int hash_for_shared_dictionary(address ptr);
  static void set_shared_class_misc_info(InstanceKlass* k, ClassFileStream* cfs);
  static InstanceKlass* lookup_from_stream(Symbol* class_name,
                                           Handle class_loader,
                                           Handle protection_domain,
                                           const ClassFileStream* cfs,
                                           TRAPS);

  DEBUG_ONLY(static bool no_class_loading_should_happen() {return _no_class_loading_should_happen;})

#ifdef ASSERT
  class NoClassLoadingMark: public StackObj {
  public:
    NoClassLoadingMark() {
      assert(!_no_class_loading_should_happen, "must not be nested");
      _no_class_loading_should_happen = true;
    }
    ~NoClassLoadingMark() {
      _no_class_loading_should_happen = false;
    }
  };
#endif

  template <typename T>
  static unsigned int hash_for_shared_dictionary_quick(T* ptr) {
    assert(((MetaspaceObj*)ptr)->is_shared(), "must be");
    assert(ptr > (T*)SharedBaseAddress, "must be");
    uintx offset = uintx(ptr) - uintx(SharedBaseAddress);
    return primitive_hash<uintx>(offset);
  }

  static const RunTimeSharedClassInfo* find_record(RunTimeSharedDictionary* dynamic_dict, Symbol* name);
  static InstanceKlass* acquire_class_for_current_thread(InstanceKlass *ik,
                                                  Handle class_loader,
                                                  Handle protection_domain,
                                                  const ClassFileStream *cfs,
                                                  TRAPS);

  static InstanceKlass* find_dynamic_builtin_class(Symbol* name);
};

#endif // SHARE_VM_CLASSFILE_SYSTEMDICTIONARYSHARED_HPP
