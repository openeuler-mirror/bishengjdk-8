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
    private static final int UB_READY_BATCH_SIZE = 32;
    private static final int UB_READY_HEADER_SIZE = 8;
    private static final int UB_READY_EVENT_SIZE = 8;
    private static final int UB_READY_FD_OFFSET = 0;
    private static final int UB_READY_GENERATION_OFFSET = 4;

    // This detects a broken protocol-802 final-offset handoff. Normal EOF
    // readiness is determined by the final offset, not by elapsed time.
    private static final long UB_NANOS_PER_MILLI = 1000L * 1000L;
    private static final long UB_CLOSE_FAILURE_NANOS =
        1000L * UB_NANOS_PER_MILLI;

    // Native drainUbSocketControl return codes.
    private static final int UB_CONTROL_ERROR = -1;
    private static final int UB_CONTROL_EOF = -2;
    private static final int UB_CONTROL_NONE = 0;
    private static final int UB_CONTROL_CLOSE = 1;

    // Profile mode and event ids must stay in sync with ubSocketProfile.hpp.
    private static final int UB_PROFILE_DETAIL = 2;
    private static final int UB_PROF_SELECTOR_READY_INJECT = 15;
    private static final int UB_PROF_SELECTOR_POLL_READY_INJECT = 20;
    // Existing events through 25 retain their IDs; later events are HotSpot-owned.
    private static boolean ubProfileDetail;
    private static boolean ubSpinEnabled;

    // Special value to indicate that an update should be ignored
    private static final byte  KILLED = (byte)-1;

    // Initial size of arrays for fd registration changes
    private static final int INITIAL_PENDING_UPDATE_SIZE = 64;

    // maximum size of updatesLow
    private static final int MAX_UPDATE_ARRAY_SIZE = AccessController.doPrivileged(
        new GetIntegerAction("sun.nio.ch.maxUpdateArraySize", Math.min(OPEN_MAX, 64*1024)));

    // The fd of the epoll driver
    private final int epfd;

    // Internal eventfd written by the process-level UBSocket poller.
    private final int ubNotifyFD;
    private boolean ubPollingNotified;
    private final AllocatedNativeObject ubReadyArray;
    private final long ubReadyArrayAddress;

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
    private final BitSet ubReadInterest = new BitSet();
    private final BitSet ubReady = new BitSet();
    private final BitSet ubClosePending = new BitSet();
    private final Map<Integer,Long> ubSubscriptions = new HashMap<>();
    private final long[] ubCloseDeadlineNsLow = new long[MAX_UPDATE_ARRAY_SIZE];
    private Map<Integer,Long> ubCloseDeadlineNsHigh;


    EPollArrayWrapper() throws IOException {
        // creates the epoll file descriptor
        epfd = epollCreate();

        // the epoll_event array passed to epoll_wait
        int allocationSize = NUM_EPOLLEVENTS * SIZE_EPOLLEVENT;
        pollArray = new AllocatedNativeObject(allocationSize, true);
        pollArrayAddress = pollArray.address();

        int notifyFD = -1;
        boolean notifyRegistered = false;
        AllocatedNativeObject readyArray = null;
        try {
            notifyFD = createUbNotifyFD();
            if (notifyFD >= 0) {
                readyArray = new AllocatedNativeObject(
                    UB_READY_HEADER_SIZE +
                    UB_READY_BATCH_SIZE * UB_READY_EVENT_SIZE, true);
                epollCtl(epfd, EPOLL_CTL_ADD, notifyFD, EPOLLIN);
                notifyRegistered = registerUbNotifyFD(notifyFD);
                if (!notifyRegistered) {
                    throw new IOException(
                        "Unable to register UBSocket selector notifier");
                }
            }
        } catch (IOException ioe) {
            if (notifyRegistered) {
                unregisterUbNotifyFD(notifyFD);
            }
            if (notifyFD >= 0) {
                try {
                    FileDispatcherImpl.closeIntFD(notifyFD);
                } catch (IOException closeFailure) {
                    ioe.addSuppressed(closeFailure);
                }
            }
            if (readyArray != null) {
                readyArray.free();
            }
            pollArray.free();
            try {
                FileDispatcherImpl.closeIntFD(epfd);
            } catch (IOException closeFailure) {
                ioe.addSuppressed(closeFailure);
            }
            throw ioe;
        }
        ubNotifyFD = notifyFD;
        ubReadyArray = readyArray;
        ubReadyArrayAddress = readyArray == null ? 0L : readyArray.address();

        // eventHigh needed when using file descriptors > 64k
        if (OPEN_MAX > MAX_UPDATE_ARRAY_SIZE) {
            eventsHigh = new HashMap<>();
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
        IOException failure = null;
        if (ubNotifyFD >= 0) {
            unregisterUbNotifyFD(ubNotifyFD);
            ubSubscriptions.clear();
            epollCtl(epfd, EPOLL_CTL_DEL, ubNotifyFD, 0);
            try {
                FileDispatcherImpl.closeIntFD(ubNotifyFD);
            } catch (IOException ioe) {
                failure = ioe;
            }
        }
        try {
            FileDispatcherImpl.closeIntFD(epfd);
        } catch (IOException ioe) {
            if (failure == null) {
                failure = ioe;
            } else {
                failure.addSuppressed(ioe);
            }
        }
        pollArray.free();
        if (ubReadyArray != null) {
            ubReadyArray.free();
        }
        if (failure != null) {
            throw failure;
        }
    }

    int poll(long timeout) throws IOException {
        updateRegistrations();
        long deadlineNs = timeout > 0
            ? System.nanoTime() + timeout * UB_NANOS_PER_MILLI
            : 0L;
        for (;;) {
            int injected = injectUbReadyEvents(0);
            injected = injectUbCloseEvents(injected);
            if (injected > 0) {
                updated = injected;
                break;
            }

            long waitTimeout = remainingUserTimeout(timeout, deadlineNs);
            if (timeout > 0 && waitTimeout == 0) {
                updated = 0;
                break;
            }
            if (ubClosePending.nextSetBit(0) >= 0) {
                waitTimeout = ubCloseWaitTimeout(waitTimeout);
            }

            updated = epollWait(pollArrayAddress, NUM_EPOLLEVENTS, waitTimeout, epfd);
            ubPollingNotified = false;
            updated = processUbNotifierEvents(updated);
            updated = processUbControlEvents(0, updated);
            if (ubPollingNotified) {
                updated = injectUbQueuedEvents(updated);
            }
            if (updated != 0 || timeout == 0) {
                break;
            }
            // Internal eventfd notifications and close deadlines are not user
            // timeouts. Continue with the original select timeout.
            if (ubPollingNotified || ubClosePending.nextSetBit(0) >= 0) {
                continue;
            }
            break;
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

    private long ubCloseWaitTimeout(long userWaitTimeout) {
        long now = System.nanoTime();
        long closeWaitMs = Long.MAX_VALUE;
        int fd = ubClosePending.nextSetBit(0);
        while (fd >= 0) {
            long deadline = getUbCloseDeadlineNs(fd);
            if (deadline > 0L) {
                long remainingNs = deadline - now;
                if (remainingNs <= 0L) {
                    return 0L;
                }
                long remainingMs = (remainingNs + UB_NANOS_PER_MILLI - 1L)
                    / UB_NANOS_PER_MILLI;
                if (remainingMs < closeWaitMs) {
                    closeWaitMs = remainingMs;
                }
            }
            fd = ubClosePending.nextSetBit(fd + 1);
        }
        if (closeWaitMs == Long.MAX_VALUE) {
            return userWaitTimeout;
        }
        return userWaitTimeout < 0L
            ? closeWaitMs : Math.min(userWaitTimeout, closeWaitMs);
    }

    private int injectUbReadyEvents(int count) {
        int fd = ubReady.nextSetBit(0);
        while (fd >= 0 && count < NUM_EPOLLEVENTS) {
            int next = ubReady.nextSetBit(fd + 1);
            if (!registered.get(fd) || !isUbSocketAttached(fd)) {
                clearUbSelectorState(fd);
            } else if (!ubReadInterested(fd)) {
                ubReady.clear(fd);
            } else {
                boolean pending = ubSocketHasPendingData(fd);
                if (!pending && ubSpinEnabled) {
                    pending = ubSocketSpinWait(fd);
                }
                if (pending) {
                    putDescriptor(count, fd);
                    putEventOps(count, EPOLLIN);
                    ubSocketProfileCountIfEnabled(UB_PROF_SELECTOR_READY_INJECT);
                    count++;
                } else {
                    ubReady.clear(fd);
                }
            }
            fd = next;
        }
        return count;
    }

    private int injectUbCloseEvents(int count) {
        int fd = ubClosePending.nextSetBit(0);
        while (fd >= 0 && count < NUM_EPOLLEVENTS) {
            int next = ubClosePending.nextSetBit(fd + 1);
            if (!registered.get(fd) || !isUbSocketAttached(fd)) {
                clearUbSelectorState(fd);
            } else if (ubReadInterested(fd) && ubSocketHasPendingData(fd)) {
                ubReady.set(fd);
                if (!resultContainsFd(count, fd)) {
                    putDescriptor(count, fd);
                    putEventOps(count, EPOLLIN);
                    count++;
                }
            } else if (ubSocketPeerCloseReady(fd)) {
                makeUbEofReady(fd);
                if (!resultContainsFd(count, fd)) {
                    putDescriptor(count, fd);
                    putEventOps(count, EPOLLIN);
                    count++;
                }
            } else if (ubCloseDeadlineReached(fd)) {
                failUbSocketClose(fd);
                clearUbSelectorState(fd);
                if (!resultContainsFd(count, fd)) {
                    putDescriptor(count, fd);
                    putEventOps(count, EPOLLIN);
                    count++;
                }
            }
            fd = next;
        }
        return count;
    }

    private boolean ubReadInterested(int fd) {
        return ubReadInterest.get(fd);
    }

    private void clearUbSelectorState(int fd) {
        Long subscription = ubSubscriptions.remove(Integer.valueOf(fd));
        if (subscription != null && subscription.longValue() > 0L) {
            unsubscribeUbReadInterest(subscription.longValue());
        }
        ubReadInterest.clear(fd);
        ubReady.clear(fd);
        ubClosePending.clear(fd);
        setUbCloseDeadlineNs(fd, 0L);
    }

    private void markUbClosePending(int fd) {
        if (!ubClosePending.get(fd)) {
            setUbCloseDeadlineNs(fd, System.nanoTime() + UB_CLOSE_FAILURE_NANOS);
        }
        ubClosePending.set(fd);
    }

    private void deferUbClose(int fd) {
        ubReady.clear(fd);
        markUbClosePending(fd);
    }

    private void makeUbEofReady(int fd) {
        ubReady.clear(fd);
        ubClosePending.clear(fd);
        setUbCloseDeadlineNs(fd, 0L);
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

    private int processUbNotifierEvents(int end) throws IOException {
        if (ubNotifyFD < 0) {
            return end;
        }
        int dst = 0;
        for (int i = 0; i < end; i++) {
            int fd = getDescriptor(i);
            if (fd == ubNotifyFD) {
                drainUbNotifyFD(ubNotifyFD);
                ubPollingNotified = true;
                continue;
            }
            if (dst != i) {
                putDescriptor(dst, fd);
                putEventOps(dst, getEventOps(i));
            }
            dst++;
        }
        return dst;
    }

    private int injectUbQueuedEvents(int count) throws IOException {
        boolean injected = false;
        long profileCompleteNs = 0L;
        while (count < NUM_EPOLLEVENTS) {
            int capacity = Math.min(
                UB_READY_BATCH_SIZE, NUM_EPOLLEVENTS - count);
            int readyCount = drainUbReadyEvents(
                ubNotifyFD, ubReadyArrayAddress, capacity);
            long batchProfileCompleteNs = ubReadyArray.getLong(0);
            if (batchProfileCompleteNs > 0L) {
                profileCompleteNs = batchProfileCompleteNs;
            }
            for (int i = 0; i < readyCount; i++) {
                int offset = UB_READY_HEADER_SIZE + i * UB_READY_EVENT_SIZE;
                int fd = ubReadyArray.getInt(offset + UB_READY_FD_OFFSET);
                int generation = ubReadyArray.getInt(
                    offset + UB_READY_GENERATION_OFFSET);
                Long subscription = ubSubscriptions.get(Integer.valueOf(fd));
                if (!registered.get(fd) || !ubReadInterested(fd) ||
                    subscription == null ||
                    (int)(subscription.longValue() >>> 32) != generation) {
                    continue;
                }
                ubReady.set(fd);
                int existing = resultIndexForFd(count, fd);
                if (existing >= 0) {
                    putEventOps(existing, getEventOps(existing) | EPOLLIN);
                    ubSocketProfileCountIfEnabled(
                        UB_PROF_SELECTOR_POLL_READY_INJECT);
                    injected = true;
                } else if (count < NUM_EPOLLEVENTS) {
                    putDescriptor(count, fd);
                    putEventOps(count, EPOLLIN);
                    ubSocketProfileCountIfEnabled(
                        UB_PROF_SELECTOR_POLL_READY_INJECT);
                    count++;
                    injected = true;
                }
            }
            if (readyCount < capacity) {
                break;
            }
        }
        if (ubProfileDetail && profileCompleteNs > 0L) {
            ubSocketProfileSelectorInjected(
                profileCompleteNs, injected);
        }
        return count;
    }

    private boolean resultContainsFd(int count, int fd) {
        return resultIndexForFd(count, fd) >= 0;
    }

    private int resultIndexForFd(int count, int fd) {
        for (int i = 0; i < count; i++) {
            if (getDescriptor(i) == fd) {
                return i;
            }
        }
        return -1;
    }

    private int processUbControlEvents(int start, int end) {
        int dst = start;
        for (int i = start; i < end; i++) {
            int ops = getEventOps(i);
            int fd = getDescriptor(i);
            boolean keep = true;

            if (fd != incomingInterruptFD && isUbSocketAttached(fd)) {
                boolean closeEvent = (ops & EPOLLCLOSE) != 0;
                int control = UB_CONTROL_NONE;
                if ((ops & EPOLLIN) != 0) {
                    control = drainUbSocketControl(fd);
                }
                boolean controlClosed = closeEvent ||
                    control == UB_CONTROL_EOF || control == UB_CONTROL_CLOSE;
                if (controlClosed) {
                    markUbSocketControlClosed(fd);
                    deferUbClose(fd);
                    ops &= ~(EPOLLIN | EPOLLCLOSE);
                    if (ubSocketHasPendingData(fd)) {
                        ubReady.set(fd);
                        ops |= EPOLLIN;
                    }
                    keep = ops != 0;
                } else if (control == UB_CONTROL_ERROR) {
                    // Preserve the kernel event so the channel observes the
                    // underlying TCP error.
                    keep = true;
                } else if ((ops & EPOLLIN) != 0) {
                    // Partial CLOSE bytes are internal control data.
                    ops &= ~EPOLLIN;
                    keep = ops != 0;
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
    private void updateRegistrations() throws IOException {
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

    private void updateUbInterestState(int fd, short events) throws IOException {
        boolean readInterested = registered.get(fd)
            && events != KILLED && (events & EPOLLIN) != 0;
        if (!readInterested) {
            clearUbSelectorState(fd);
            return;
        }

        boolean wasReadInterested = ubReadInterest.get(fd);
        Long existing = ubSubscriptions.get(Integer.valueOf(fd));
        long subscription = existing == null ? 0L : existing.longValue();
        if (subscription == 0L) {
            subscription = subscribeUbReadInterest(fd, ubNotifyFD);
            if (subscription < 0L) {
                throw new IOException(
                    "Unable to subscribe UBSocket fd to Selector notifier");
            }
            if (subscription == 0L) {
                clearUbSelectorState(fd);
                return;
            }
            ubSubscriptions.put(
                Integer.valueOf(fd), Long.valueOf(subscription));
        }

        ubReadInterest.set(fd);
        // Register before checking the Ring. A publication before registration
        // is found here; a later publication is routed through the subscription.
        if (!wasReadInterested && ubSocketHasPendingData(fd)) {
            ubReady.set(fd);
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
        ubSpinEnabled = ubSocketSpinEnabled();
    }

    private static native int createUbNotifyFD() throws IOException;
    private static native boolean registerUbNotifyFD(int fd);
    private static native void unregisterUbNotifyFD(int fd);
    private static native void drainUbNotifyFD(int fd) throws IOException;
    private static native int drainUbReadyEvents(int fd, long address,
                                                  int capacity) throws IOException;
    private static native long subscribeUbReadInterest(int dataFD, int eventFD);
    private static native void unsubscribeUbReadInterest(long subscriptionID);
    private native int epollCreate();
    private native void epollCtl(int epfd, int opcode, int fd, int events);
    private native int epollWait(long pollAddress, int numfds, long timeout,
                                 int epfd) throws IOException;
    private static native int drainUbSocketControl(int fd);
    private static native boolean isUbSocketAttached(int fd);
    private static native boolean ubSocketHasPendingData(int fd);
    private static native boolean ubSocketPeerCloseReady(int fd);
    private static native void failUbSocketClose(int fd);
    private static native boolean ubSocketSpinEnabled();
    private static native boolean ubSocketSpinWait(int fd);
    private static native int ubSocketProfileMode();
    private static native void ubSocketProfileCount(int event);
    private static native void ubSocketProfileSelectorInjected(
        long completeNs, boolean injected);
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
