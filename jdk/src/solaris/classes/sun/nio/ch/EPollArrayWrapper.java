/*
 * Copyright (c) 2005, 2013, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
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

package sun.nio.ch;

import java.io.IOException;
import java.security.AccessController;
import java.util.BitSet;
import java.util.HashMap;
import java.util.Map;
import sun.security.action.GetIntegerAction;

/**
 * Manipulates a native array of epoll_event structs on Linux:
 *
 * typedef union epoll_data {
 *     void *ptr;
 *     int fd;
 *     __uint32_t u32;
 *     __uint64_t u64;
 *  } epoll_data_t;
 *
 * struct epoll_event {
 *     __uint32_t events;
 *     epoll_data_t data;
 * };
 *
 * The system call to wait for I/O events is epoll_wait(2). It populates an
 * array of epoll_event structures that are passed to the call. The data
 * member of the epoll_event structure contains the same data as was set
 * when the file descriptor was registered to epoll via epoll_ctl(2). In
 * this implementation we set data.fd to be the file descriptor that we
 * register. That way, we have the file descriptor available when we
 * process the events.
 */

class EPollArrayWrapper {
    // EPOLL_EVENTS
    private static final int EPOLLIN      = 0x001;
    private static final int EPOLLERR     = 0x008;
    private static final int EPOLLHUP     = 0x010;
    private static final int EPOLLRDHUP   = 0x2000;
    private static final int EPOLLCLOSE   = EPOLLERR | EPOLLHUP | EPOLLRDHUP;

    // opcodes
    private static final int EPOLL_CTL_ADD      = 1;
    private static final int EPOLL_CTL_DEL      = 2;
    private static final int EPOLL_CTL_MOD      = 3;

    // Miscellaneous constants
    private static final int SIZE_EPOLLEVENT  = sizeofEPollEvent();
    private static final int EVENT_OFFSET     = 0;
    private static final int DATA_OFFSET      = offsetofData();
    private static final int FD_OFFSET        = DATA_OFFSET;
    private static final int OPEN_MAX         = IOUtil.fdLimit();
    private static final int NUM_EPOLLEVENTS  = Math.min(OPEN_MAX, 8192);
    private static final int UB_PROBE_POLL_MS = 1;
    private static final int UB_PROBE_SPIN_LIMIT = 8;
    private static final long UB_NANOS_PER_MILLI = 1000L * 1000L;
    private static final long UB_CLOSE_PENDING_NANOS = UB_NANOS_PER_MILLI;
    private static final int UB_DRAIN_ERROR = -1;
    private static final int UB_DRAIN_EOF = -2;
    private static final int UB_PROFILE_DETAIL = 2;
    private static final int UB_PROF_SELECTOR_PROBE_CHECK = 27;
    private static final int UB_PROF_SELECTOR_PROBE_READY = 28;
    private static final int UB_PROF_SELECTOR_PROBE_EMPTY = 29;
    private static final int UB_PROF_SELECTOR_READY_INJECT = 30;
    private static boolean ubProfileDetail;

    // Special value to indicate that an update should be ignored
    private static final byte  KILLED = (byte)-1;

    // Initial size of arrays for fd registration changes
    private static final int INITIAL_PENDING_UPDATE_SIZE = 64;

    // maximum size of updatesLow
    private static final int MAX_UPDATE_ARRAY_SIZE = AccessController.doPrivileged(
        new GetIntegerAction("sun.nio.ch.maxUpdateArraySize", Math.min(OPEN_MAX, 64*1024)));

    // The fd of the epoll driver
    private final int epfd;

     // The epoll_event array for results from epoll_wait
    private final AllocatedNativeObject pollArray;

    // Base address of the epoll_event array
    private final long pollArrayAddress;

    // The fd of the interrupt line going out
    private int outgoingInterruptFD;

    // The fd of the interrupt line coming in
    private int incomingInterruptFD;

    // The index of the interrupt FD
    private int interruptedIndex;

    // Number of updated pollfd entries
    int updated;

    // object to synchronize fd registration changes
    private final Object updateLock = new Object();

    // number of file descriptors with registration changes pending
    private int updateCount;

    // file descriptors with registration changes pending
    private int[] updateDescriptors = new int[INITIAL_PENDING_UPDATE_SIZE];

    // events for file descriptors with registration changes pending, indexed
    // by file descriptor and stored as bytes for efficiency reasons. For
    // file descriptors higher than MAX_UPDATE_ARRAY_SIZE (unlimited case at
    // least) then the update is stored in a map.
    private final byte[] eventsLow = new byte[MAX_UPDATE_ARRAY_SIZE];
    private Map<Integer,Byte> eventsHigh;

    // Used by release and updateRegistrations to track whether a file
    // descriptor is registered with epoll.
    private final BitSet registered = new BitSet();
    private final BitSet ubReady = new BitSet();
    private final BitSet ubProbe = new BitSet();
    private final BitSet ubClosePending = new BitSet();
    private final byte[] ubProbeMissesLow = new byte[MAX_UPDATE_ARRAY_SIZE];
    private final long[] ubCloseDeadlineNsLow = new long[MAX_UPDATE_ARRAY_SIZE];
    private Map<Integer,Byte> ubProbeMissesHigh;
    private Map<Integer,Long> ubCloseDeadlineNsHigh;


    EPollArrayWrapper() throws IOException {
        // creates the epoll file descriptor
        epfd = epollCreate();

        // the epoll_event array passed to epoll_wait
        int allocationSize = NUM_EPOLLEVENTS * SIZE_EPOLLEVENT;
        pollArray = new AllocatedNativeObject(allocationSize, true);
        pollArrayAddress = pollArray.address();

        // eventHigh needed when using file descriptors > 64k
        if (OPEN_MAX > MAX_UPDATE_ARRAY_SIZE) {
            eventsHigh = new HashMap<>();
            ubProbeMissesHigh = new HashMap<>();
        }
    }

    void initInterrupt(int fd0, int fd1) {
        outgoingInterruptFD = fd1;
        incomingInterruptFD = fd0;
        epollCtl(epfd, EPOLL_CTL_ADD, fd0, EPOLLIN);
    }

    void putEventOps(int i, int event) {
        int offset = SIZE_EPOLLEVENT * i + EVENT_OFFSET;
        pollArray.putInt(offset, event);
    }

    void putDescriptor(int i, int fd) {
        int offset = SIZE_EPOLLEVENT * i + FD_OFFSET;
        pollArray.putInt(offset, fd);
    }

    int getEventOps(int i) {
        int offset = SIZE_EPOLLEVENT * i + EVENT_OFFSET;
        return pollArray.getInt(offset);
    }

    int getDescriptor(int i) {
        int offset = SIZE_EPOLLEVENT * i + FD_OFFSET;
        return pollArray.getInt(offset);
    }

    /**
     * Returns {@code true} if updates for the given key (file
     * descriptor) are killed.
     */
    private boolean isEventsHighKilled(Integer key) {
        assert key >= MAX_UPDATE_ARRAY_SIZE;
        Byte value = eventsHigh.get(key);
        return (value != null && value == KILLED);
    }

    /**
     * Sets the pending update events for the given file descriptor. This
     * method has no effect if the update events is already set to KILLED,
     * unless {@code force} is {@code true}.
     */
    private void setUpdateEvents(int fd, byte events, boolean force) {
        if (fd < MAX_UPDATE_ARRAY_SIZE) {
            if ((eventsLow[fd] != KILLED) || force) {
                eventsLow[fd] = events;
            }
        } else {
            Integer key = Integer.valueOf(fd);
            if (!isEventsHighKilled(key) || force) {
                eventsHigh.put(key, Byte.valueOf(events));
            }
        }
    }

    /**
     * Returns the pending update events for the given file descriptor.
     */
    private byte getUpdateEvents(int fd) {
        if (fd < MAX_UPDATE_ARRAY_SIZE) {
            return eventsLow[fd];
        } else {
            Byte result = eventsHigh.get(Integer.valueOf(fd));
            // result should never be null
            return result.byteValue();
        }
    }

    /**
     * Update the events for a given file descriptor
     */
    void setInterest(int fd, int mask) {
        synchronized (updateLock) {
            // record the file descriptor and events
            int oldCapacity = updateDescriptors.length;
            if (updateCount == oldCapacity) {
                int newCapacity = oldCapacity + INITIAL_PENDING_UPDATE_SIZE;
                int[] newDescriptors = new int[newCapacity];
                System.arraycopy(updateDescriptors, 0, newDescriptors, 0, oldCapacity);
                updateDescriptors = newDescriptors;
            }
            updateDescriptors[updateCount++] = fd;

            // events are stored as bytes for efficiency reasons
            byte b = (byte)mask;
            assert (b == mask) && (b != KILLED);
            setUpdateEvents(fd, b, false);
        }
    }

    /**
     * Add a file descriptor
     */
    void add(int fd) {
        // force the initial update events to 0 as it may be KILLED by a
        // previous registration.
        synchronized (updateLock) {
            assert !registered.get(fd);
            setUpdateEvents(fd, (byte)0, true);
        }
    }

    /**
     * Remove a file descriptor
     */
    void remove(int fd) {
        synchronized (updateLock) {
            // kill pending and future update for this file descriptor
            setUpdateEvents(fd, KILLED, false);

            // remove from epoll
            if (registered.get(fd)) {
                epollCtl(epfd, EPOLL_CTL_DEL, fd, 0);
                registered.clear(fd);
            }
            clearUbSelectorState(fd);
        }
    }

    /**
     * Close epoll file descriptor and free poll array
     */
    void closeEPollFD() throws IOException {
        FileDispatcherImpl.closeIntFD(epfd);
        pollArray.free();
    }

    int poll(long timeout) throws IOException {
        updateRegistrations();
        long deadlineNs = timeout > 0
            ? System.nanoTime() + timeout * UB_NANOS_PER_MILLI
            : 0L;
        for (;;) {
            int injected = injectUbReadyEvents();
            if (injected == 0) {
                injected = injectUbProbeEvents();
            }
            if (injected > 0) {
                updated = processUbWakeups(0, injected);
                break;
            }

            long waitTimeout = ubProbe.nextSetBit(0) >= 0
                ? ubProbeWaitTimeout(timeout, deadlineNs)
                : remainingUserTimeout(timeout, deadlineNs);
            if (timeout > 0 && waitTimeout == 0) {
                updated = 0;
                break;
            }

            updated = epollWait(pollArrayAddress, NUM_EPOLLEVENTS, waitTimeout, epfd);
            updated = processUbWakeups(0, updated);
            if (updated != 0 || timeout == 0 || ubProbe.nextSetBit(0) < 0) {
                break;
            }
        }
        for (int i=0; i<updated; i++) {
            if (getDescriptor(i) == incomingInterruptFD) {
                interruptedIndex = i;
                interrupted = true;
                break;
            }
        }
        return updated;
    }

    private long remainingUserTimeout(long timeout, long deadlineNs) {
        if (timeout <= 0) {
            return timeout;
        }
        long remainingNs = deadlineNs - System.nanoTime();
        if (remainingNs <= 0) {
            return 0;
        }
        long remainingMs = remainingNs / UB_NANOS_PER_MILLI;
        return remainingMs == 0 ? 1 : remainingMs;
    }

    private long ubProbeWaitTimeout(long timeout, long deadlineNs) {
        if (timeout == 0) {
            return 0;
        }
        if (hasUbProbeSpinBudget()) {
            return 0;
        }
        if (timeout < 0) {
            return UB_PROBE_POLL_MS;
        }
        long remaining = remainingUserTimeout(timeout, deadlineNs);
        return remaining < UB_PROBE_POLL_MS ? remaining : UB_PROBE_POLL_MS;
    }

    private int injectUbReadyEvents() {
        int count = 0;
        int fd = ubReady.nextSetBit(0);
        while (fd >= 0 && count < NUM_EPOLLEVENTS) {
            int next = ubReady.nextSetBit(fd + 1);
            if (!registered.get(fd) || !isUbSocketAttached(fd)) {
                clearUbSelectorState(fd);
            } else if (!ubReadInterested(fd)) {
                ubReady.clear(fd);
                clearUbProbe(fd);
            } else if (ubSocketHasPendingData(fd)) {
                putDescriptor(count, fd);
                putEventOps(count, EPOLLIN);
                ubSocketProfileCountIfEnabled(UB_PROF_SELECTOR_READY_INJECT);
                count++;
            } else {
                ubReady.clear(fd);
                armUbProbe(fd);
            }
            fd = next;
        }
        return count;
    }

    private int injectUbProbeEvents() {
        int count = 0;
        int fd = ubProbe.nextSetBit(0);
        while (fd >= 0 && count < NUM_EPOLLEVENTS) {
            int next = ubProbe.nextSetBit(fd + 1);
            ubSocketProfileCountIfEnabled(UB_PROF_SELECTOR_PROBE_CHECK);
            if (!registered.get(fd) || !isUbSocketAttached(fd)) {
                clearUbSelectorState(fd);
            } else if (!ubReadInterested(fd)) {
                ubReady.clear(fd);
                clearUbProbe(fd);
            } else if (ubSocketHasPendingData(fd)) {
                clearUbProbe(fd);
                ubReady.set(fd);
                putDescriptor(count, fd);
                putEventOps(count, EPOLLIN);
                ubSocketProfileCountIfEnabled(UB_PROF_SELECTOR_PROBE_READY);
                ubSocketProfileCountIfEnabled(UB_PROF_SELECTOR_READY_INJECT);
                count++;
            } else if (ubClosePending.get(fd) && ubCloseDeadlineReached(fd) &&
                       getUbProbeMisses(fd) >= UB_PROBE_SPIN_LIMIT) {
                finishUbClose(fd);
                putDescriptor(count, fd);
                putEventOps(count, EPOLLIN);
                count++;
            } else {
                noteUbProbeEmpty(fd);
            }
            fd = next;
        }
        return count;
    }

    private void armUbProbe(int fd) {
        ubProbe.set(fd);
        setUbProbeMisses(fd, 0);
    }

    private void clearUbProbe(int fd) {
        ubProbe.clear(fd);
        setUbProbeMisses(fd, 0);
    }

    private void noteUbProbeEmpty(int fd) {
        ubSocketProfileCountIfEnabled(UB_PROF_SELECTOR_PROBE_EMPTY);
        int misses = getUbProbeMisses(fd) + 1;
        setUbProbeMisses(fd, misses);
    }

    private boolean ubReadInterested(int fd) {
        short events = getUpdateEvents(fd);
        return events != KILLED && (events & EPOLLIN) != 0;
    }

    private void clearUbSelectorState(int fd) {
        ubReady.clear(fd);
        ubClosePending.clear(fd);
        setUbCloseDeadlineNs(fd, 0L);
        clearUbProbe(fd);
    }

    private void markUbClosePending(int fd) {
        if (!ubClosePending.get(fd)) {
            setUbCloseDeadlineNs(fd, System.nanoTime() + UB_CLOSE_PENDING_NANOS);
        }
        ubClosePending.set(fd);
    }

    private void deferUbClose(int fd) {
        ubReady.clear(fd);
        markUbClosePending(fd);
        if (!ubProbe.get(fd)) {
            armUbProbe(fd);
        }
    }

    private void finishUbClose(int fd) {
        ubReady.clear(fd);
        ubClosePending.clear(fd);
        setUbCloseDeadlineNs(fd, 0L);
        clearUbProbe(fd);
        unregisterUbSocket(fd);
    }

    private boolean hasUbProbeSpinBudget() {
        int fd = ubProbe.nextSetBit(0);
        while (fd >= 0) {
            if (getUbProbeMisses(fd) < UB_PROBE_SPIN_LIMIT) {
                return true;
            }
            fd = ubProbe.nextSetBit(fd + 1);
        }
        return false;
    }

    private int getUbProbeMisses(int fd) {
        if (fd < ubProbeMissesLow.length) {
            return ubProbeMissesLow[fd] & 0xff;
        }
        Byte misses = ubProbeMissesHigh == null ? null : ubProbeMissesHigh.get(fd);
        return misses == null ? 0 : misses.byteValue() & 0xff;
    }

    private void setUbProbeMisses(int fd, int misses) {
        if (fd < ubProbeMissesLow.length) {
            ubProbeMissesLow[fd] = (byte)misses;
            return;
        }
        if (ubProbeMissesHigh == null) {
            ubProbeMissesHigh = new HashMap<>();
        }
        if (misses == 0) {
            ubProbeMissesHigh.remove(fd);
        } else {
            ubProbeMissesHigh.put(fd, (byte)misses);
        }
    }

    private boolean ubCloseDeadlineReached(int fd) {
        long deadline = getUbCloseDeadlineNs(fd);
        return deadline != 0L && System.nanoTime() >= deadline;
    }

    private long getUbCloseDeadlineNs(int fd) {
        if (fd < ubCloseDeadlineNsLow.length) {
            return ubCloseDeadlineNsLow[fd];
        }
        Long deadline = ubCloseDeadlineNsHigh == null ? null
            : ubCloseDeadlineNsHigh.get(fd);
        return deadline == null ? 0L : deadline.longValue();
    }

    private void setUbCloseDeadlineNs(int fd, long deadline) {
        if (fd < ubCloseDeadlineNsLow.length) {
            ubCloseDeadlineNsLow[fd] = deadline;
            return;
        }
        if (ubCloseDeadlineNsHigh == null) {
            ubCloseDeadlineNsHigh = new HashMap<>();
        }
        if (deadline == 0L) {
            ubCloseDeadlineNsHigh.remove(fd);
        } else {
            ubCloseDeadlineNsHigh.put(fd, Long.valueOf(deadline));
        }
    }

    private int processUbWakeups(int start, int end) {
        int dst = start;
        for (int i = start; i < end; i++) {
            int ops = getEventOps(i);
            int fd = getDescriptor(i);
            boolean closeEvent = (ops & EPOLLCLOSE) != 0;
            boolean keep = true;

            if (fd == incomingInterruptFD) {
                keep = true;
            } else if ((ops & EPOLLIN) == 0) {
                if (closeEvent && isUbSocketAttached(fd)) {
                    markUbSocketControlClosed(fd);
                    if (ubSocketHasPendingData(fd)) {
                        markUbClosePending(fd);
                        ubReady.set(fd);
                        clearUbProbe(fd);
                        ops = EPOLLIN;
                    } else {
                        deferUbClose(fd);
                        keep = false;
                    }
                }
            } else {
                int drained = drainUbSocketWakeups(fd);
                boolean ubAttached = isUbSocketAttached(fd);
                boolean controlClosed = closeEvent || drained == UB_DRAIN_EOF;
                if (ubAttached && controlClosed) {
                    markUbSocketControlClosed(fd);
                }
                if (ubAttached && ubSocketHasPendingData(fd)) {
                    if (controlClosed) {
                        markUbClosePending(fd);
                    }
                    ubReady.set(fd);
                    clearUbProbe(fd);
                    if (controlClosed) {
                        ops = EPOLLIN;
                    }
                } else {
                    ubReady.clear(fd);
                    if (ubAttached && controlClosed) {
                        deferUbClose(fd);
                        keep = false;
                    } else if (drained == UB_DRAIN_ERROR) {
                        clearUbProbe(fd);
                        keep = true;
                    } else if (drained > 0 && !closeEvent) {
                        armUbProbe(fd);
                        ops &= ~EPOLLIN;
                        keep = ops != 0;
                    } else if (closeEvent) {
                        clearUbProbe(fd);
                    }
                }
            }

            if (!keep)
                continue;
            if (dst != i) {
                putDescriptor(dst, fd);
            }
            putEventOps(dst, ops);
            dst++;
        }
        return dst;
    }

    /**
     * Update the pending registrations.
     */
    private void updateRegistrations() {
        synchronized (updateLock) {
            int j = 0;
            while (j < updateCount) {
                int fd = updateDescriptors[j];
                short events = getUpdateEvents(fd);
                boolean isRegistered = registered.get(fd);
                int opcode = 0;

                if (events != KILLED) {
                    if (isRegistered) {
                        opcode = (events != 0) ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
                    } else {
                        opcode = (events != 0) ? EPOLL_CTL_ADD : 0;
                    }
                    if (opcode != 0) {
                        epollCtl(epfd, opcode, fd, events);
                        if (opcode == EPOLL_CTL_ADD) {
                            registered.set(fd);
                        } else if (opcode == EPOLL_CTL_DEL) {
                            registered.clear(fd);
                            clearUbSelectorState(fd);
                        }
                        updateUbInterestState(fd, events);
                    }
                }
                j++;
            }
            updateCount = 0;
        }
    }

    private void updateUbInterestState(int fd, short events) {
        if (!registered.get(fd) || !isUbSocketAttached(fd)) {
            clearUbSelectorState(fd);
            return;
        }
        if (events == KILLED || (events & EPOLLIN) == 0) {
            ubReady.clear(fd);
            clearUbProbe(fd);
            return;
        }
        if (ubSocketHasPendingData(fd)) {
            ubReady.set(fd);
            clearUbProbe(fd);
        } else if (ubClosePending.get(fd)) {
            armUbProbe(fd);
        }
    }

    // interrupt support
    private boolean interrupted = false;

    public void interrupt() {
        interrupt(outgoingInterruptFD);
    }

    public int interruptedIndex() {
        return interruptedIndex;
    }

    boolean interrupted() {
        return interrupted;
    }

    void clearInterrupted() {
        interrupted = false;
    }

    static {
        IOUtil.load();
        init();
        ubProfileDetail = ubSocketProfileMode() >= UB_PROFILE_DETAIL;
    }

    private native int epollCreate();
    private native void epollCtl(int epfd, int opcode, int fd, int events);
    private native int epollWait(long pollAddress, int numfds, long timeout,
                                 int epfd) throws IOException;
    private static native int drainUbSocketWakeups(int fd);
    private static native boolean isUbSocketAttached(int fd);
    private static native boolean ubSocketHasPendingData(int fd);
    private static native int ubSocketProfileMode();
    private static native void ubSocketProfileCount(int event);
    private static native void unregisterUbSocket(int fd);
    private static native void markUbSocketControlClosed(int fd);
    private static native int sizeofEPollEvent();
    private static native int offsetofData();
    private static native void interrupt(int fd);
    private static native void init();

    private static void ubSocketProfileCountIfEnabled(int event) {
        if (ubProfileDetail) {
            ubSocketProfileCount(event);
        }
    }
}
