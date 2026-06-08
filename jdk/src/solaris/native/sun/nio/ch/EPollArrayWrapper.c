/*
 * Copyright (c) 2005, 2016, Oracle and/or its affiliates. All rights reserved.
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

#include "jni.h"
#include "jni_util.h"
#include "jvm.h"
#include "jlong.h"

#include "sun_nio_ch_EPollArrayWrapper.h"

#include <unistd.h>
#include <sys/time.h>
#include <sys/epoll.h>
#include <errno.h>
#include <time.h>

#define UB_SOCKET_PARSE_BATCH_BUF_LEN 4096

enum UBSocketDrainResult {
    UB_SOCKET_DRAIN_ERROR = -1,
    UB_SOCKET_DRAIN_EOF = -2
};

enum {
    UB_PROFILE_DETAIL = 2,
    UB_PROF_WAKEUP_DRAIN_TOTAL = 14,
    UB_PROF_WAKEUP_DRAIN_SYSCALL = 15,
    UB_PROF_SELECTOR_PENDING_CHECK = 17,
    UB_PROF_SELECTOR_PENDING_READY = 18,
    UB_PROF_SELECTOR_PROBE_CHECK = 27,
    UB_PROF_SELECTOR_PROBE_READY = 28,
    UB_PROF_SELECTOR_PROBE_EMPTY = 29,
    UB_PROF_SELECTOR_READY_INJECT = 30
};

static const jlong UB_NANOS_PER_SECOND = 1000000000LL;

#define RESTARTABLE(_cmd, _result) do { \
  do { \
    _result = _cmd; \
  } while((_result == -1) && (errno == EINTR)); \
} while(0)


static int
iepoll(int epfd, struct epoll_event *events, int numfds, jlong timeout)
{
    jlong start, now;
    int remaining = timeout;
    struct timeval t;
    int diff;

    gettimeofday(&t, NULL);
    start = t.tv_sec * 1000 + t.tv_usec / 1000;

    for (;;) {
        int res = epoll_wait(epfd, events, numfds, remaining);
        if (res < 0 && errno == EINTR) {
            if (remaining >= 0) {
                gettimeofday(&t, NULL);
                now = t.tv_sec * 1000 + t.tv_usec / 1000;
                diff = now - start;
                remaining -= diff;
                if (diff < 0 || remaining <= 0) {
                    return 0;
                }
                start = now;
            }
        } else {
            return res;
        }
    }
}

JNIEXPORT void JNICALL
Java_sun_nio_ch_EPollArrayWrapper_init(JNIEnv *env, jclass this)
{
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_EPollArrayWrapper_epollCreate(JNIEnv *env, jobject this)
{
    /*
     * epoll_create expects a size as a hint to the kernel about how to
     * dimension internal structures. We can't predict the size in advance.
     */
    int epfd = epoll_create(256);
    if (epfd < 0) {
       JNU_ThrowIOExceptionWithLastError(env, "epoll_create failed");
    }
    return epfd;
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_EPollArrayWrapper_sizeofEPollEvent(JNIEnv* env, jclass this)
{
    return sizeof(struct epoll_event);
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_EPollArrayWrapper_offsetofData(JNIEnv* env, jclass this)
{
    return offsetof(struct epoll_event, data);
}

JNIEXPORT void JNICALL
Java_sun_nio_ch_EPollArrayWrapper_epollCtl(JNIEnv *env, jobject this, jint epfd,
                                           jint opcode, jint fd, jint events)
{
    struct epoll_event event;
    int res;

    event.events = events;
    event.data.fd = fd;

    RESTARTABLE(epoll_ctl(epfd, (int)opcode, (int)fd, &event), res);

    /*
     * A channel may be registered with several Selectors. When each Selector
     * is polled a EPOLL_CTL_DEL op will be inserted into its pending update
     * list to remove the file descriptor from epoll. The "last" Selector will
     * close the file descriptor which automatically unregisters it from each
     * epoll descriptor. To avoid costly synchronization between Selectors we
     * allow pending updates to be processed, ignoring errors. The errors are
     * harmless as the last update for the file descriptor is guaranteed to
     * be EPOLL_CTL_DEL.
     */
    if (res < 0 && errno != EBADF && errno != ENOENT && errno != EPERM) {
        JNU_ThrowIOExceptionWithLastError(env, "epoll_ctl failed");
    }
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_EPollArrayWrapper_epollWait(JNIEnv *env, jobject this,
                                            jlong address, jint numfds,
                                            jlong timeout, jint epfd)
{
    struct epoll_event *events = jlong_to_ptr(address);
    int res;

    if (timeout <= 0) {           /* Indefinite or no wait */
        RESTARTABLE(epoll_wait(epfd, events, numfds, timeout), res);
    } else {                      /* Bounded wait; bounded restarts */
        res = iepoll(epfd, events, numfds, timeout);
    }

    if (res < 0) {
        JNU_ThrowIOExceptionWithLastError(env, "epoll_wait failed");
    }
    return res;
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_EPollArrayWrapper_drainUbSocketWakeups(JNIEnv *env, jclass this,
                                                       jint fd)
{
    char buf[UB_SOCKET_PARSE_BATCH_BUF_LEN];
    jint profile_mode;
    jint parsed_any = 0;

    if ((*env)->IsUbSocketReady(env, fd) == JNI_FALSE) {
        return 0;
    }

    profile_mode = (*env)->UbSocketProfileMode(env);
    for (;;) {
        ssize_t nread;
        jlong parsed;
        jlong total_start = 0;
        jlong syscall_start = 0;

        if (profile_mode >= UB_PROFILE_DETAIL) {
            struct timespec ts;
            if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
                total_start = (jlong)ts.tv_sec * UB_NANOS_PER_SECOND +
                              (jlong)ts.tv_nsec;
                syscall_start = total_start;
            }
        }

        nread = read(fd, buf, sizeof(buf));

        if (profile_mode >= UB_PROFILE_DETAIL && syscall_start != 0) {
            struct timespec ts;
            if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
                jlong now = (jlong)ts.tv_sec * UB_NANOS_PER_SECOND +
                             (jlong)ts.tv_nsec;
                (*env)->UbSocketProfileRecord(env, UB_PROF_WAKEUP_DRAIN_SYSCALL,
                                              now - syscall_start,
                                              nread > 0 ? nread : 0, 1);
            }
        }

        if (nread < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return parsed_any;
            }
            return UB_SOCKET_DRAIN_ERROR;
        }
        if (nread == 0) {
            return UB_SOCKET_DRAIN_EOF;
        }

        parsed = (*env)->UbSocketParse(env, fd, buf, (jint)nread);
        if (parsed < 0) {
            return UB_SOCKET_DRAIN_ERROR;
        }
        if (parsed > 0) {
            parsed_any = 1;
        }

        if (profile_mode >= UB_PROFILE_DETAIL && total_start != 0) {
            struct timespec ts;
            if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
                jlong now = (jlong)ts.tv_sec * UB_NANOS_PER_SECOND +
                             (jlong)ts.tv_nsec;
                (*env)->UbSocketProfileRecord(env, UB_PROF_WAKEUP_DRAIN_TOTAL,
                                              now - total_start, nread, 1);
            }
        }
    }
}

JNIEXPORT void JNICALL
Java_sun_nio_ch_EPollArrayWrapper_ubSocketProfileCount(JNIEnv *env, jclass this,
                                                       jint event)
{
    if ((*env)->UbSocketProfileMode(env) >= UB_PROFILE_DETAIL) {
        (*env)->UbSocketProfileRecord(env, event, 0, 0, 1);
    }
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_EPollArrayWrapper_ubSocketProfileMode(JNIEnv *env, jclass this)
{
    return (*env)->UbSocketProfileMode(env);
}

JNIEXPORT void JNICALL
Java_sun_nio_ch_EPollArrayWrapper_unregisterUbSocket(JNIEnv *env, jclass this,
                                                     jint fd)
{
    (*env)->UbSocketDetach(env, fd);
}

JNIEXPORT void JNICALL
Java_sun_nio_ch_EPollArrayWrapper_markUbSocketControlClosed(JNIEnv *env,
                                                            jclass this,
                                                            jint fd)
{
    (*env)->UbSocketMarkControlClosed(env, fd);
}

JNIEXPORT jboolean JNICALL
Java_sun_nio_ch_EPollArrayWrapper_isUbSocketAttached(JNIEnv *env, jclass this,
                                                     jint fd)
{
    return (*env)->IsUbSocket(env, fd);
}

JNIEXPORT jboolean JNICALL
Java_sun_nio_ch_EPollArrayWrapper_ubSocketHasPendingData(JNIEnv *env, jclass this,
                                                         jint fd)
{
    jboolean ready;
    jint profile_mode = (*env)->UbSocketProfileMode(env);
    if (profile_mode >= UB_PROFILE_DETAIL) {
        (*env)->UbSocketProfileRecord(env, UB_PROF_SELECTOR_PENDING_CHECK, 0, 0, 1);
    }
    ready = (*env)->UbSocketHasPendingData(env, fd);
    if (ready == JNI_TRUE && profile_mode >= UB_PROFILE_DETAIL) {
        (*env)->UbSocketProfileRecord(env, UB_PROF_SELECTOR_PENDING_READY, 0, 0, 1);
    }
    return ready;
}

JNIEXPORT void JNICALL
Java_sun_nio_ch_EPollArrayWrapper_interrupt(JNIEnv *env, jobject this, jint fd)
{
    int fakebuf[1];
    fakebuf[0] = 1;
    if (write(fd, fakebuf, 1) < 0) {
        JNU_ThrowIOExceptionWithLastError(env,"write to interrupt fd failed");
    }
}
