/*
 * Copyright (c) 1999, 2020, Oracle and/or its affiliates. All rights reserved.
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

#ifndef OS_LINUX_VM_OS_LINUX_HPP
#define OS_LINUX_VM_OS_LINUX_HPP

// Linux_OS defines the interface to Linux operating systems

/* pthread_getattr_np comes with LinuxThreads-0.9-7 on RedHat 7.1 */
typedef int (*pthread_getattr_func_type) (pthread_t, pthread_attr_t *);

// Information about the protection of the page at address '0' on this os.
static bool zero_page_read_protected() { return true; }

class Linux {
  friend class CgroupSubsystem;
  friend class os;
  friend class OSContainer;
  friend class TestReserveMemorySpecial;

  // For signal-chaining
#define MAXSIGNUM 32
  static struct sigaction sigact[MAXSIGNUM]; // saved preinstalled sigactions
  static unsigned int sigs;             // mask of signals that have
                                        // preinstalled signal handlers
  static bool libjsig_is_loaded;        // libjsig that interposes sigaction(),
                                        // __sigaction(), signal() is loaded
  static struct sigaction *(*get_signal_action)(int);
  static struct sigaction *get_preinstalled_handler(int);
  static void save_preinstalled_handler(int, struct sigaction&);

  static void check_signal_handler(int sig);

  // For signal flags diagnostics
  static int sigflags[MAXSIGNUM];

  static int (*_clock_gettime)(clockid_t, struct timespec *);
  static int (*_pthread_getcpuclockid)(pthread_t, clockid_t *);
  static int (*_pthread_setname_np)(pthread_t, const char*);

  static address   _initial_thread_stack_bottom;
  static uintptr_t _initial_thread_stack_size;

  static const char *_glibc_version;
  static const char *_libpthread_version;

  static bool _is_floating_stack;
  static bool _is_NPTL;
  static bool _supports_fast_thread_cpu_time;

  static GrowableArray<int>* _cpu_to_node;
  static GrowableArray<int>* _nindex_to_node;

 protected:

  static julong _physical_memory;
  static pthread_t _main_thread;
  static Mutex* _createThread_lock;
  static int _page_size;
  static const int _vm_default_page_size;

  static julong available_memory();
  static int active_processor_count();

  static void initialize_system_info();

  static int commit_memory_impl(char* addr, size_t bytes, bool exec);
  static int commit_memory_impl(char* addr, size_t bytes,
                                size_t alignment_hint, bool exec);

  static void set_glibc_version(const char *s)      { _glibc_version = s; }
  static void set_libpthread_version(const char *s) { _libpthread_version = s; }

  static bool supports_variable_stack_size();

  static void set_is_NPTL()                   { _is_NPTL = true;  }
  static void set_is_LinuxThreads()           { _is_NPTL = false; }
  static void set_is_floating_stack()         { _is_floating_stack = true; }

  static void rebuild_cpu_to_node_map();
  static void rebuild_nindex_to_node_map();
  static GrowableArray<int>* cpu_to_node()    { return _cpu_to_node; }
  static GrowableArray<int>* nindex_to_node()  { return _nindex_to_node; }

  static size_t find_large_page_size();
  static size_t setup_large_page_size();

  static bool setup_large_page_type(size_t page_size);
  static bool transparent_huge_pages_sanity_check(bool warn, size_t pages_size);
  static bool hugetlbfs_sanity_check(bool warn, size_t page_size);

  static char* reserve_memory_special_shm(size_t bytes, size_t alignment, char* req_addr, bool exec);
  static char* reserve_memory_special_huge_tlbfs(size_t bytes, size_t alignment, char* req_addr, bool exec);
  static char* reserve_memory_special_huge_tlbfs_only(size_t bytes, char* req_addr, bool exec);
  static char* reserve_memory_special_huge_tlbfs_mixed(size_t bytes, size_t alignment, char* req_addr, bool exec);

  static bool release_memory_special_impl(char* base, size_t bytes);
  static bool release_memory_special_shm(char* base, size_t bytes);
  static bool release_memory_special_huge_tlbfs(char* base, size_t bytes);

  static void print_process_memory_info(outputStream* st);
  static void print_system_memory_info(outputStream* st);
  static void print_container_info(outputStream* st);
  static void print_distro_info(outputStream* st);
  static void print_libversion_info(outputStream* st);
  static void print_system_process_count(outputStream* st);
  static void print_proc_sys_info(outputStream* st);

 public:
  static bool _stack_is_executable;
  static void *dlopen_helper(const char *name, char *ebuf, int ebuflen);
  static void *dll_load_in_vmthread(const char *name, char *ebuf, int ebuflen);

  static void init_thread_fpu_state();
  static int  get_fpu_control_word();
  static void set_fpu_control_word(int fpu_control);
  static pthread_t main_thread(void)                                { return _main_thread; }
  // returns kernel thread id (similar to LWP id on Solaris), which can be
  // used to access /proc
  static pid_t gettid();
  static void set_createThread_lock(Mutex* lk)                      { _createThread_lock = lk; }
  static Mutex* createThread_lock(void)                             { return _createThread_lock; }
  static void hotspot_sigmask(Thread* thread);

  static address   initial_thread_stack_bottom(void)                { return _initial_thread_stack_bottom; }
  static uintptr_t initial_thread_stack_size(void)                  { return _initial_thread_stack_size; }

  static int page_size(void)                                        { return _page_size; }
  static void set_page_size(int val)                                { _page_size = val; }

  static int vm_default_page_size(void)                             { return _vm_default_page_size; }

  static address   ucontext_get_pc(ucontext_t* uc);
  static intptr_t* ucontext_get_sp(ucontext_t* uc);
  static intptr_t* ucontext_get_fp(ucontext_t* uc);

  static julong physical_memory() { return _physical_memory; }
  static julong host_swap();

  // For Analyzer Forte AsyncGetCallTrace profiling support:
  //
  // This interface should be declared in os_linux_i486.hpp, but
  // that file provides extensions to the os class and not the
  // Linux class.
  static ExtendedPC fetch_frame_from_ucontext(Thread* thread, ucontext_t* uc,
    intptr_t** ret_sp, intptr_t** ret_fp);

  // This boolean allows users to forward their own non-matching signals
  // to JVM_handle_linux_signal, harmlessly.
  static bool signal_handlers_are_installed;

  static int get_our_sigflags(int);
  static void set_our_sigflags(int, int);
  static void signal_sets_init();
  static void install_signal_handlers();
  static void set_signal_handler(int, bool);
  static bool is_sig_ignored(int sig);

  static sigset_t* unblocked_signals();
  static sigset_t* vm_signals();
  static sigset_t* allowdebug_blocked_signals();

  // For signal-chaining
  static struct sigaction *get_chained_signal_action(int sig);
  static bool chained_handler(int sig, siginfo_t* siginfo, void* context);

  // GNU libc and libpthread version strings
  static const char *glibc_version()          { return _glibc_version; }
  static const char *libpthread_version()     { return _libpthread_version; }

  // NPTL or LinuxThreads?
  static bool is_LinuxThreads()               { return !_is_NPTL; }
  static bool is_NPTL()                       { return _is_NPTL;  }

  // NPTL is always floating stack. LinuxThreads could be using floating
  // stack or fixed stack.
  static bool is_floating_stack()             { return _is_floating_stack; }

  static void load_ACC_library();
  static void load_ACC_library_before_ergo();
  static void libpthread_init();
  static void parse_numa_nodes();
  static bool libnuma_init();
  static void* libnuma_dlsym(void* handle, const char* name);
  // libnuma v2 (libnuma_1.2) symbols
  static void* libnuma_v2_dlsym(void* handle, const char* name);
  // Minimum stack size a thread can be created with (allowing
  // the VM to completely create the thread and enter user code)
  static size_t min_stack_allowed;

  // Return default stack size or guard size for the specified thread type
  static size_t default_stack_size(os::ThreadType thr_type);
  static size_t default_guard_size(os::ThreadType thr_type);

  static void capture_initial_stack(size_t max_size);

  // Stack overflow handling
  static bool manually_expand_stack(JavaThread * t, address addr);
  static int max_register_window_saves_before_flushing();

  // Real-time clock functions
  static void clock_init(void);

  // fast POSIX clocks support
  static void fast_thread_clock_init(void);

  static inline bool supports_monotonic_clock() {
    return _clock_gettime != NULL;
  }

  static int clock_gettime(clockid_t clock_id, struct timespec *tp) {
    return _clock_gettime ? _clock_gettime(clock_id, tp) : -1;
  }

  static int pthread_getcpuclockid(pthread_t tid, clockid_t *clock_id) {
    return _pthread_getcpuclockid ? _pthread_getcpuclockid(tid, clock_id) : -1;
  }

  static bool supports_fast_thread_cpu_time() {
    return _supports_fast_thread_cpu_time;
  }

  static jlong fast_thread_cpu_time(clockid_t clockid);

  // pthread_cond clock suppport
  private:
  static pthread_condattr_t _condattr[1];

  public:
  static pthread_condattr_t* condAttr() { return _condattr; }

  // Output structure for query_process_memory_info() (all values in KB)
  struct meminfo_t {
    ssize_t vmsize;     // current virtual size
    ssize_t vmpeak;     // peak virtual size
    ssize_t vmrss;      // current resident set size
    ssize_t vmhwm;      // peak resident set size
    ssize_t vmswap;     // swapped out
    ssize_t rssanon;    // resident set size (anonymous mappings, needs 4.5)
    ssize_t rssfile;    // resident set size (file mappings, needs 4.5)
    ssize_t rssshmem;   // resident set size (shared mappings, needs 4.5)
  };

  // Attempts to query memory information about the current process and return it in the output structure.
  // May fail (returns false) or succeed (returns true) but not all output fields are available; unavailable
  // fields will contain -1.
  static bool query_process_memory_info(meminfo_t* info);

  // Stack repair handling

  // none present

  // LinuxThreads work-around for 6292965
  static int safe_cond_timedwait(pthread_cond_t *_cond, pthread_mutex_t *_mutex, const struct timespec *_abstime);

private:
  static void expand_stack_to(address bottom);

  typedef int (*sched_getcpu_func_t)(void);
  typedef int (*numa_node_to_cpus_func_t)(int node, unsigned long *buffer, int bufferlen);
  typedef int (*numa_max_node_func_t)(void);
  typedef int (*numa_num_configured_nodes_func_t)(void);
  typedef int (*numa_available_func_t)(void);
  typedef int (*numa_tonode_memory_func_t)(void *start, size_t size, int node);
  typedef void (*numa_interleave_memory_func_t)(void *start, size_t size, unsigned long *nodemask);
  typedef void (*numa_interleave_memory_v2_func_t)(void *start, size_t size, struct bitmask* mask);
  typedef struct bitmask* (*numa_get_membind_func_t)(void);
  typedef struct bitmask* (*numa_get_interleave_mask_func_t)(void);
  typedef long (*numa_move_pages_func_t)(int pid, unsigned long count, void **pages, const int *nodes, int *status, int flags);
  typedef int (*numa_run_on_node_func_t)(int node);
  typedef struct bitmask* (*numa_parse_nodestring_all_func_t)(const char*);
  typedef int (*numa_run_on_node_mask_func_t)(struct bitmask* mask);
  typedef void (*numa_set_membind_func_t)(struct bitmask* mask);
  typedef int (*numa_bitmask_equal_func_t)(struct bitmask* mask, struct bitmask* mask1);
  typedef void (*numa_bitmask_free_func_t)(struct bitmask* mask);

  typedef void (*numa_set_bind_policy_func_t)(int policy);
  typedef int (*numa_bitmask_isbitset_func_t)(struct bitmask *bmp, unsigned int n);
  typedef int (*numa_distance_func_t)(int node1, int node2);

  typedef void* (*heap_dict_add_t)(void* key, void* val, void* heap_dict, uint8_t type);
  typedef void* (*heap_dict_lookup_t)(void* key, void* heap_dict, bool deletable);
  typedef void (*heap_dict_free_t)(void* heap_dict, bool is_nested);
  typedef void* (*heap_vector_add_t)(void* val, void* heap_vector, bool &_inserted);
  typedef void* (*heap_vector_get_next_t)(void* heap_vector, void* heap_vector_node, int &_cnt, void** &_items);
  typedef void (*heap_vector_free_t)(void* heap_vector);
  typedef bool (*dmh_g1_can_shrink_t)(double used_after_gc_d, size_t _new_max_heap, double maximum_used_percentage, size_t max_heap_size);
  typedef uint (*dmh_g1_get_region_limit_t)(size_t _new_max_heap, size_t region_size);
  static heap_dict_add_t _heap_dict_add;
  static heap_dict_lookup_t _heap_dict_lookup;
  static heap_dict_free_t _heap_dict_free;
  static heap_vector_add_t _heap_vector_add;
  static heap_vector_get_next_t _heap_vector_get_next;
  static heap_vector_free_t _heap_vector_free;
  static sched_getcpu_func_t _sched_getcpu;
  static numa_node_to_cpus_func_t _numa_node_to_cpus;
  static numa_max_node_func_t _numa_max_node;
  static numa_num_configured_nodes_func_t _numa_num_configured_nodes;
  static numa_available_func_t _numa_available;
  static numa_tonode_memory_func_t _numa_tonode_memory;
  static numa_interleave_memory_func_t _numa_interleave_memory;
  static numa_interleave_memory_v2_func_t _numa_interleave_memory_v2;
  static numa_set_bind_policy_func_t _numa_set_bind_policy;
  static numa_bitmask_isbitset_func_t _numa_bitmask_isbitset;
  static numa_distance_func_t _numa_distance;
  static numa_get_membind_func_t _numa_get_membind;
  static numa_get_interleave_mask_func_t _numa_get_interleave_mask;
  static numa_move_pages_func_t _numa_move_pages;
  static numa_run_on_node_func_t _numa_run_on_node;
  static numa_parse_nodestring_all_func_t _numa_parse_nodestring_all;
  static numa_run_on_node_mask_func_t _numa_run_on_node_mask;
  static numa_bitmask_equal_func_t _numa_bitmask_equal;
  static numa_set_membind_func_t _numa_set_membind;
  static numa_bitmask_free_func_t _numa_bitmask_free;
  static dmh_g1_can_shrink_t _dmh_g1_can_shrink;
  static dmh_g1_get_region_limit_t _dmh_g1_get_region_limit;

  static unsigned long* _numa_all_nodes;
  static struct bitmask* _numa_all_nodes_ptr;
  static struct bitmask* _numa_nodes_ptr;
  static struct bitmask* _numa_interleave_bitmask;
  static struct bitmask* _numa_membind_bitmask;

  static void set_sched_getcpu(sched_getcpu_func_t func) { _sched_getcpu = func; }
  static void set_numa_node_to_cpus(numa_node_to_cpus_func_t func) { _numa_node_to_cpus = func; }
  static void set_numa_max_node(numa_max_node_func_t func) { _numa_max_node = func; }
  static void set_numa_num_configured_nodes(numa_num_configured_nodes_func_t func) { _numa_num_configured_nodes = func; }
  static void set_numa_available(numa_available_func_t func) { _numa_available = func; }
  static void set_numa_tonode_memory(numa_tonode_memory_func_t func) { _numa_tonode_memory = func; }
  static void set_numa_interleave_memory(numa_interleave_memory_func_t func) { _numa_interleave_memory = func; }
  static void set_numa_interleave_memory_v2(numa_interleave_memory_v2_func_t func) { _numa_interleave_memory_v2 = func; }
  static void set_numa_set_bind_policy(numa_set_bind_policy_func_t func) { _numa_set_bind_policy = func; }
  static void set_numa_bitmask_isbitset(numa_bitmask_isbitset_func_t func) { _numa_bitmask_isbitset = func; }
  static void set_numa_distance(numa_distance_func_t func) { _numa_distance = func; }
  static void set_numa_get_membind(numa_get_membind_func_t func) { _numa_get_membind = func; }
  static void set_numa_get_interleave_mask(numa_get_interleave_mask_func_t func) { _numa_get_interleave_mask = func; }
  static void set_numa_move_pages(numa_move_pages_func_t func) { _numa_move_pages = func; }
  static void set_numa_run_on_node(numa_run_on_node_func_t func) { _numa_run_on_node = func; }
  static void set_numa_parse_nodestring_all(numa_parse_nodestring_all_func_t func) { _numa_parse_nodestring_all = func; }
  static void set_numa_run_on_node_mask(numa_run_on_node_mask_func_t func) { _numa_run_on_node_mask = func; }
  static void set_numa_bitmask_equal(numa_bitmask_equal_func_t func) { _numa_bitmask_equal = func; }
  static void set_numa_set_membind(numa_set_membind_func_t func) { _numa_set_membind = func; }
  static void set_numa_bitmask_free(numa_bitmask_free_func_t func) { _numa_bitmask_free = func; }
  static void set_numa_all_nodes(unsigned long* ptr) { _numa_all_nodes = ptr; }
  static void set_numa_all_nodes_ptr(struct bitmask **ptr) { _numa_all_nodes_ptr = (ptr == NULL ? NULL : *ptr); }
  static void set_numa_nodes_ptr(struct bitmask **ptr) { _numa_nodes_ptr = (ptr == NULL ? NULL : *ptr); }
  static void set_numa_interleave_bitmask(struct bitmask* ptr)     { _numa_interleave_bitmask = ptr ;   }
  static void set_numa_membind_bitmask(struct bitmask* ptr)        { _numa_membind_bitmask = ptr ;      }
  static int sched_getcpu_syscall(void);

  enum NumaAllocationPolicy{
    NotInitialized,
    Membind,
    Interleave
  };
  static NumaAllocationPolicy _current_numa_policy;

public:
  static int sched_getcpu()  { return _sched_getcpu != NULL ? _sched_getcpu() : -1; }
  static int numa_node_to_cpus(int node, unsigned long *buffer, int bufferlen) {
    return _numa_node_to_cpus != NULL ? _numa_node_to_cpus(node, buffer, bufferlen) : -1;
  }
  static int numa_max_node() { return _numa_max_node != NULL ? _numa_max_node() : -1; }
  static int numa_num_configured_nodes() {
    return _numa_num_configured_nodes != NULL ? _numa_num_configured_nodes() : -1;
  }
  static int numa_available() { return _numa_available != NULL ? _numa_available() : -1; }
  static int numa_tonode_memory(void *start, size_t size, int node) {
    return _numa_tonode_memory != NULL ? _numa_tonode_memory(start, size, node) : -1;
  }

  static void set_configured_numa_policy(NumaAllocationPolicy numa_policy) {
    _current_numa_policy = numa_policy;
  }

  static NumaAllocationPolicy identify_numa_policy() {
    for (int node = 0; node <= Linux::numa_max_node(); node++) {
      if (Linux::_numa_bitmask_isbitset(Linux::_numa_interleave_bitmask, node)) {
        return Interleave;
      }
    }
    return Membind;
  }

  static void numa_interleave_memory(void *start, size_t size) {
    // Use v2 api if available
    if (_numa_interleave_memory_v2 != NULL && _numa_all_nodes_ptr != NULL) {
      _numa_interleave_memory_v2(start, size, _numa_all_nodes_ptr);
    } else if (_numa_interleave_memory != NULL && _numa_all_nodes != NULL) {
      _numa_interleave_memory(start, size, _numa_all_nodes);
    }
  }
  static void numa_set_bind_policy(int policy) {
    if (_numa_set_bind_policy != NULL) {
      _numa_set_bind_policy(policy);
    }
  }
  static int numa_distance(int node1, int node2) {
    return _numa_distance != NULL ? _numa_distance(node1, node2) : -1;
  }
  static int numa_run_on_node(int node) {
    return _numa_run_on_node != NULL ? _numa_run_on_node(node) : -1;
  }

  static long numa_move_pages(int pid, unsigned long count, void **pages, const int *nodes, int *status, int flags) {
    return _numa_move_pages != NULL ? _numa_move_pages(pid, count, pages, nodes, status, flags) : -1;
  }

  static int get_node_by_cpu(int cpu_id);
  static int get_existing_num_nodes();
  // Check if numa node is configured (non-zero memory node).
  static bool isnode_in_configured_nodes(unsigned int n) {
    if (_numa_bitmask_isbitset != NULL && _numa_all_nodes_ptr != NULL) {
      return _numa_bitmask_isbitset(_numa_all_nodes_ptr, n);
    } else
      return 0;
  }
  // Check if numa node exists in the system (including zero memory nodes).
  static bool isnode_in_existing_nodes(unsigned int n) {
    if (_numa_bitmask_isbitset != NULL && _numa_nodes_ptr != NULL) {
      return _numa_bitmask_isbitset(_numa_nodes_ptr, n);
    } else if (_numa_bitmask_isbitset != NULL && _numa_all_nodes_ptr != NULL) {
      // Not all libnuma API v2 implement numa_nodes_ptr, so it's not possible
      // to trust the API version for checking its absence. On the other hand,
      // numa_nodes_ptr found in libnuma 2.0.9 and above is the only way to get
      // a complete view of all numa nodes in the system, hence numa_nodes_ptr
      // is used to handle CPU and nodes on architectures (like PowerPC) where
      // there can exist nodes with CPUs but no memory or vice-versa and the
      // nodes may be non-contiguous. For most of the architectures, like
      // x86_64, numa_node_ptr presents the same node set as found in
      // numa_all_nodes_ptr so it's possible to use numa_all_nodes_ptr as a
      // substitute.
      return _numa_bitmask_isbitset(_numa_all_nodes_ptr, n);
    } else
      return 0;
  }
  // Check if node is in bound node set.
  static bool isnode_in_bound_nodes(int node) {
    if (_numa_membind_bitmask != NULL && _numa_bitmask_isbitset != NULL) {
      return _numa_bitmask_isbitset(_numa_membind_bitmask, node);
    } else {
      return false;
    }
  }
  // Check if bound to only one numa node.
  // Returns true if bound to a single numa node, otherwise returns false.
  static bool isbound_to_single_node() {
    int nodes = 0;
    unsigned int node = 0;
    unsigned int highest_node_number = 0;

    if (_numa_membind_bitmask != NULL && _numa_max_node != NULL && _numa_bitmask_isbitset != NULL) {
      highest_node_number = _numa_max_node();
    } else {
      return false;
    }

    for (node = 0; node <= highest_node_number; node++) {
      if (_numa_bitmask_isbitset(_numa_membind_bitmask, node)) {
        nodes++;
      }
    }

    if (nodes == 1) {
      return true;
    } else {
      return false;
    }
  }

#ifdef __GLIBC__
  struct glibc_mallinfo2 {
    size_t arena;
    size_t ordblks;
    size_t smblks;
    size_t hblks;
    size_t hblkhd;
    size_t usmblks;
    size_t fsmblks;
    size_t uordblks;
    size_t fordblks;
    size_t keepcost;
  };
  enum mallinfo_retval_t { ok, error, ok_but_possibly_wrapped };
  // get_mallinfo() is a wrapper for mallinfo/mallinfo2. It will prefer mallinfo2() if found.
  // If we only have mallinfo(), values may be 32-bit truncated, which is signaled via
  // "ok_but_possibly_wrapped".
  static mallinfo_retval_t get_mallinfo(glibc_mallinfo2* out);

  // Calls out to GNU extension malloc_info if available
  // otherwise does nothing and returns -2.
  static int malloc_info(FILE* stream);
#endif

  static bool isbound_to_all_node() {
    if (_numa_membind_bitmask != NULL && _numa_max_node != NULL && _numa_bitmask_isbitset != NULL) {
      unsigned int highest_node_number = _numa_max_node();
      for (unsigned int node = 0; node <= highest_node_number; node++) {
        if (!_numa_bitmask_isbitset(_numa_membind_bitmask, node)) {
          return false;
        }
      }
    }
    return true;
  }

  static bitmask* numa_parse_nodestring_all(const char* s) {
    return _numa_parse_nodestring_all != NULL ? _numa_parse_nodestring_all(s) : NULL;
  }

  static int numa_run_on_node_mask(bitmask* bitmask) {
    return _numa_run_on_node_mask != NULL ? _numa_run_on_node_mask(bitmask) : -1;
  }

  static int numa_bitmask_equal(bitmask* bitmask, struct bitmask* bitmask1) {
    return _numa_bitmask_equal != NULL ? _numa_bitmask_equal(bitmask, bitmask1) : 1;
  }

  static void numa_set_membind(bitmask* bitmask) {
    if (_numa_set_membind != NULL) {
      _numa_set_membind(bitmask);
    }
  }

  static void numa_bitmask_free(bitmask* bitmask) {
    if (_numa_bitmask_free != NULL) {
      _numa_bitmask_free(bitmask);
    }
  }

  static void* heap_dict_add(void* key, void* val, void* heap_dict, uint8_t type) {
    if(_heap_dict_add == NULL) {
        return NULL;
    }
    return _heap_dict_add(key, val, heap_dict, type);
  }

  static void* heap_dict_lookup(void* key, void* heap_dict, bool deletable) {
    if(_heap_dict_lookup == NULL) {
        return NULL;
    }
    return _heap_dict_lookup(key, heap_dict, deletable);
  }

  static void heap_dict_free(void* heap_dict, bool is_nested) {
    if(_heap_dict_free != NULL) {
        _heap_dict_free(heap_dict, is_nested);
    }
  }

  static void* heap_vector_add(void* val, void* heap_vector, bool &_inserted) {
    if(_heap_vector_add == NULL) {
        return NULL;
    }
    return _heap_vector_add(val, heap_vector, _inserted);
  }

  static void* heap_vector_get_next(void* heap_vector, void* heap_vector_node, int &_cnt, void** &_items) {
    if(_heap_vector_get_next == NULL) {
        return NULL;
    }
    return _heap_vector_get_next(heap_vector, heap_vector_node, _cnt, _items);
  }

  static void heap_vector_free(void* heap_vector) {
    if(_heap_vector_free != NULL) {
        _heap_vector_free(heap_vector);
    }
  }
  static bool dmh_g1_can_shrink(double used_after_gc_d,
                               size_t _new_max_heap,
                               double maximum_used_percentage,
                               size_t max_heap_size,
                               bool &is_valid,
                               bool just_check = false) {
    is_valid = false;
    bool result = false;
    if (just_check) {
      is_valid = (_dmh_g1_can_shrink != NULL);
    } else if (_dmh_g1_can_shrink != NULL) {
      is_valid = true;
      result = _dmh_g1_can_shrink(used_after_gc_d, _new_max_heap, maximum_used_percentage, max_heap_size);
    }
    return result;
  }

  static uint dmh_g1_get_region_limit(size_t _new_max_heap, size_t region_size, bool &is_valid, bool just_check = false) {
    is_valid = false;
    uint result = 0;
    if (just_check) {
      is_valid = (_dmh_g1_get_region_limit != NULL);
    } else if (_dmh_g1_get_region_limit != NULL) {
      is_valid = true;
      result = _dmh_g1_get_region_limit(_new_max_heap, region_size);
    }
    return result;
  }
};

class PlatformEvent : public CHeapObj<mtInternal> {
  private:
    double CachePad [4] ;   // increase odds that _mutex is sole occupant of cache line
    volatile int _Event ;
    volatile int _nParked ;
    pthread_mutex_t _mutex  [1] ;
    pthread_cond_t  _cond   [1] ;
    double PostPad  [2] ;
    Thread * _Assoc ;

  public:       // TODO-FIXME: make dtor private
    ~PlatformEvent() { guarantee (0, "invariant") ; }

  public:
    PlatformEvent() {
      int status;
      status = pthread_cond_init (_cond, os::Linux::condAttr());
      assert_status(status == 0, status, "cond_init");
      status = pthread_mutex_init (_mutex, NULL);
      assert_status(status == 0, status, "mutex_init");
      _Event   = 0 ;
      _nParked = 0 ;
      _Assoc   = NULL ;
    }

    // Use caution with reset() and fired() -- they may require MEMBARs
    void reset() { _Event = 0 ; }
    int  fired() { return _Event; }
    void park () ;
    void unpark () ;
    int  TryPark () ;
    int  park (jlong millis) ; // relative timed-wait only
    void SetAssociation (Thread * a) { _Assoc = a ; }
} ;

class PlatformParker : public CHeapObj<mtInternal> {
  protected:
    enum {
        REL_INDEX = 0,
        ABS_INDEX = 1
    };
    int _cur_index;  // which cond is in use: -1, 0, 1
    pthread_mutex_t _mutex [1] ;
    pthread_cond_t  _cond  [2] ; // one for relative times and one for abs.

  public:       // TODO-FIXME: make dtor private
    ~PlatformParker() { guarantee (0, "invariant") ; }

  public:
    PlatformParker() {
      int status;
      status = pthread_cond_init (&_cond[REL_INDEX], os::Linux::condAttr());
      assert_status(status == 0, status, "cond_init rel");
      status = pthread_cond_init (&_cond[ABS_INDEX], NULL);
      assert_status(status == 0, status, "cond_init abs");
      status = pthread_mutex_init (_mutex, NULL);
      assert_status(status == 0, status, "mutex_init");
      _cur_index = -1; // mark as unused
    }
};

#endif // OS_LINUX_VM_OS_LINUX_HPP
