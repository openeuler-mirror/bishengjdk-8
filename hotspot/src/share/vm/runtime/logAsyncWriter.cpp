/*
 * Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
 * Copyright (c) 2022, Huawei Technologies Co., Ltd. All rights reserved.
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
#include "precompiled.hpp"
#include "runtime/atomic.hpp"
#include "runtime/logAsyncWriter.hpp"
#include "utilities/ostream.hpp"

class AsyncLogWriter::AsyncLogLocker : public StackObj {
 public:
  AsyncLogLocker() {
    assert(_instance != NULL, "AsyncLogWriter::_lock is unavailable");
    _instance->_lock.wait();
  }

  ~AsyncLogLocker() {
    _instance->_lock.signal();
  }
};

void AsyncLogWriter::enqueue_locked(const AsyncLogMessage& msg) {
  if (_buffer.size() >= _buffer_max_size)  {
    // drop the enqueueing message.
    os::free(msg.message());
    return;
  }

  assert(_buffer.size() < _buffer_max_size, "_buffer is over-sized.");
  _buffer.push_back(msg);
  _sem.signal();
}

void AsyncLogWriter::enqueue(const char* msg) {
  AsyncLogMessage m(os::strdup(msg));

  { // critical area
    AsyncLogLocker locker;
    enqueue_locked(m);
  }
}

AsyncLogWriter::AsyncLogWriter()
  : NamedThread(),
  _lock(1), _sem(0), _io_sem(1),
  _initialized(false),_should_terminate(false),_has_terminated(false),
  _buffer_max_size(AsyncLogBufferSize / sizeof(AsyncLogMessage)) {
  if (os::create_thread(this, os::asynclog_thread)) {
    _initialized = true;
    set_name("AsyncLog Thread");
  } else {
    if (PrintAsyncGCLog) {
        tty->print_cr("AsyncLogging failed to create thread. Falling back to synchronous logging.");
    }
  }

  if (PrintAsyncGCLog) {
    tty->print_cr("The maximum entries of AsyncLogBuffer: " SIZE_FORMAT ", estimated memory use: " SIZE_FORMAT " bytes",
                      _buffer_max_size, AsyncLogBufferSize);
  }
}

void AsyncLogWriter::write() {
  // Use kind of copy-and-swap idiom here.
  // Empty 'logs' swaps the content with _buffer.
  // Along with logs destruction, all processed messages are deleted.
  //
  // The operation 'pop_all()' is done in O(1). All I/O jobs are then performed without
  // lock protection. This guarantees I/O jobs don't block logsites.
  AsyncLogBuffer logs;
  bool own_io = false;

  { // critical region
    AsyncLogLocker locker;

    _buffer.pop_all(&logs);
    own_io = _io_sem.trywait();
  }

  LinkedListIterator<AsyncLogMessage> it(logs.head());
  if (!own_io) {
    _io_sem.wait();
  }

  bool flush = false;
  while (!it.is_empty()) {
    AsyncLogMessage* e = it.next();
    char* msg = e->message();

    if (msg != NULL) {
      flush = true;
      ((gcLogFileStream*)gclog_or_tty)->write_blocking(msg, strlen(msg));
      os::free(msg);
    }
  }
  if (flush) {
    ((gcLogFileStream*)gclog_or_tty)->fileStream::flush();
  }
  _io_sem.signal();
}

void AsyncLogWriter::run() {
  while (true) {
    // The value of a semphore cannot be negative. Therefore, the current thread falls asleep
    // when its value is zero. It will be waken up when new messages are enqueued.
    _sem.wait();
    if (_should_terminate) {
      write();
      terminate();
      break;
    }
    write();
  }
}

AsyncLogWriter* AsyncLogWriter::_instance = NULL;

void AsyncLogWriter::initialize() {
  if (!UseAsyncGCLog) return;

  assert(_instance == NULL, "initialize() should only be invoked once.");

  AsyncLogWriter* self = new AsyncLogWriter();
  if (self->_initialized) {
    OrderAccess::release_store_ptr(&AsyncLogWriter::_instance, self);
    os::start_thread(self);
    if (PrintAsyncGCLog) {
      tty->print_cr("Async logging thread started.");
    }
  }
}

AsyncLogWriter* AsyncLogWriter::instance() {
  return _instance;
}

// write() acquires and releases _io_sem even _buffer is empty.
// This guarantees all logging I/O of dequeued messages are done when it returns.
void AsyncLogWriter::flush() {
  if (_instance != NULL) {
    _instance->write();
  }
}

void AsyncLogWriter::print_on(outputStream* st) const{
  st->print("\"%s\" ", name());
  Thread::print_on(st);
  st->cr();
}

void AsyncLogWriter::stop() {
  {
    MutexLockerEx ml(Terminator_lock);
    _should_terminate = true;
  }
  {
    _sem.signal();
  }
  {
    MutexLockerEx ml(Terminator_lock);
    while (!_has_terminated) {
      Terminator_lock->wait();
    }
  }
}

void AsyncLogWriter::terminate() {
  // Signal that it is terminated
  {
    MutexLockerEx mu(Terminator_lock,
                     Mutex::_no_safepoint_check_flag);
    _has_terminated = true;
    Terminator_lock->notify();
  }

  // Thread destructor usually does this..
  ThreadLocalStorage::set_thread(NULL);
}
