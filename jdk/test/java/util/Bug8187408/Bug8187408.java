/*
 * Copyright (c) 2023, Oracle and/or its affiliates. All rights reserved.
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

 /*
 * @test
 * @bug 8187408
 * @summary AbstractQueuedSynchronizer wait queue corrupted when thread awaits without holding the lock
 */

import static java.util.concurrent.TimeUnit.MILLISECONDS;
import java.util.ArrayList;
import java.util.Date;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ThreadLocalRandom;
import java.util.concurrent.locks.Condition;
import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.ReentrantLock;

public class Bug8187408 {
    static final long LONG_DELAY_MS = 10000L;

    public static void main(String[] args) throws InterruptedException {
        final ThreadLocalRandom rnd = ThreadLocalRandom.current();
        final AwaitMethod awaitMethod = randomAwaitMethod();
        final int nThreads = rnd.nextInt(2, 10);
        final ReentrantLock lock = new ReentrantLock();
        final Condition cond = lock.newCondition();
        final CountDownLatch done = new CountDownLatch(nThreads);
        final ArrayList<Thread> threads = new ArrayList<>();

        Runnable rogue = () -> {
            while (done.getCount() > 0) {
                try {
                    // call await without holding lock?!
                    await(cond, awaitMethod);
                    throw new AssertionError("should throw");
                }
                catch (IllegalMonitorStateException success) {}
                catch (Throwable fail) { threadUnexpectedException(fail); }}};
        Thread rogueThread = new Thread(rogue, "rogue");
        threads.add(rogueThread);
        rogueThread.start();

        Runnable waiter = () -> {
            lock.lock();
            try {
                done.countDown();
                cond.await();
            } catch (Throwable fail) {
                threadUnexpectedException(fail);
            } finally {
                lock.unlock();
            }};
        for (int i = 0; i < nThreads; i++) {
            Thread thread = new Thread(waiter, "waiter");
            threads.add(thread);
            thread.start();
        }

        assertTrue(done.await(LONG_DELAY_MS, MILLISECONDS));
        lock.lock();
        try {
            assertEquals(nThreads, lock.getWaitQueueLength(cond));
        } finally {
            cond.signalAll();
            lock.unlock();
        }
        for (Thread thread : threads) {
            thread.join(LONG_DELAY_MS);
            assertFalse(thread.isAlive());
        }
    }

    private static void assertTrue(boolean expr) {
        if (!expr)
            throw new RuntimeException("assertion failed");
    }

    private static void assertFalse(boolean expr) {
        if (expr)
            throw new RuntimeException("assertion failed");
    }

    private static void assertEquals(int i, int j) {
        if (i != j)
            throw new AssertionError(i + " != " + j);
    }

    /**
     * Records the given exception using {@link #threadRecordFailure},
     * then rethrows the exception, wrapping it in an AssertionError
     * if necessary.
     */
    private static void threadUnexpectedException(Throwable t) {
        t.printStackTrace();
        if (t instanceof RuntimeException)
            throw (RuntimeException) t;
        else if (t instanceof Error)
            throw (Error) t;
        else
            throw new AssertionError("unexpected exception: " + t, t);
    }

    enum AwaitMethod { await, awaitTimed, awaitNanos, awaitUntil }
    private static AwaitMethod randomAwaitMethod() {
        AwaitMethod[] awaitMethods = AwaitMethod.values();
        return awaitMethods[ThreadLocalRandom.current().nextInt(awaitMethods.length)];
    }

    /**
     * Returns a new Date instance representing a time at least
     * delayMillis milliseconds in the future.
     */
    private static Date delayedDate(long delayMillis) {
        // Add 1 because currentTimeMillis is known to round into the past.
        return new Date(System.currentTimeMillis() + delayMillis + 1);
    }

    /**
     * Awaits condition "indefinitely" using the specified AwaitMethod.
     */
    private static void await(Condition c, AwaitMethod awaitMethod)
            throws InterruptedException {
        long timeoutMillis = 2 * LONG_DELAY_MS;
        switch (awaitMethod) {
        case await:
            c.await();
            break;
        case awaitTimed:
            assertTrue(c.await(timeoutMillis, MILLISECONDS));
            break;
        case awaitNanos:
            long timeoutNanos = MILLISECONDS.toNanos(timeoutMillis);
            long nanosRemaining = c.awaitNanos(timeoutNanos);
            assertTrue(nanosRemaining > timeoutNanos / 2);
            assertTrue(nanosRemaining <= timeoutNanos);
            break;
        case awaitUntil:
            assertTrue(c.awaitUntil(delayedDate(timeoutMillis)));
            break;
        default:
            throw new AssertionError();
        }
    }
}