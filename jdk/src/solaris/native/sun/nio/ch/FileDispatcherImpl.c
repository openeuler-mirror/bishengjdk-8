/*
 * Copyright (c) 2000, 2018, Oracle and/or its affiliates. All rights reserved.
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

#if defined(__linux__)
#define _FILE_OFFSET_BITS 64
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#if defined(__linux__)
#include <linux/fs.h>
#include <sys/ioctl.h>
#endif

#if defined(_ALLBSD_SOURCE)
#define lseek64 lseek
#define stat64 stat
#define flock64 flock
#define off64_t off_t
#define F_SETLKW64 F_SETLKW
#define F_SETLK64 F_SETLK

#define pread64 pread
#define pwrite64 pwrite
#define ftruncate64 ftruncate
#define fstat64 fstat

#define fdatasync fsync
#endif

#include "jni.h"
#include "jni_util.h"
#include "jvm.h"
#include "jlong.h"
#include "nio.h"
#include "nio_util.h"
#include "sun_nio_ch_FileDispatcherImpl.h"
#include "java_lang_Long.h"

#if defined(aarch64)
  __asm__(".symver fcntl64,fcntl@GLIBC_2.17");
#elif defined(amd64)
  __asm__(".symver fcntl64,fcntl@GLIBC_2.2.5");
#endif
static int preCloseFD = -1;     /* File descriptor to which we dup other fd's
                                   before closing them for real */

// UB Matrix support: batch buffer length for parsing multiple frames
static const jint UB_SOCKET_PARSE_BATCH_BUF_LEN = 4096;
static jboolean ub_profile_enabled = JNI_FALSE;
// Notice: same as UBSocketProfileEvent in ubSocketProfile.hpp
enum {
    UB_PROF_NIO_WRITE_TOTAL = 0,
    UB_PROF_NIO_READ_TOTAL = 8,
    UB_PROF_UB_FIRST_HIT = 9,
    UB_PROF_UB_FIRST_MISS = 10,
    UB_PROF_DESCRIPTOR_RECV_TOTAL = 11,
    UB_PROF_DESCRIPTOR_RECV_SYSCALL = 12
};

static jlong
ub_nio_profile_nanos(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) { return 0; }
    return (jlong)ts.tv_sec * 1000000000LL + (jlong)ts.tv_nsec;
}

#define UB_NIO_PROFILE_START(var)                                          \
    do {                                                                   \
        (var) = ub_profile_enabled ? ub_nio_profile_nanos() : 0;           \
    } while (0)

#define UB_NIO_PROFILE_END(env, event, start_ns, bytes)                     \
    do {                                                                    \
        if ((start_ns) != 0) {                                              \
            jlong elapsed_ns = ub_nio_profile_nanos() - (start_ns);         \
            (*env)->UbSocketProfileRecord(env, (event), elapsed_ns,         \
                                          (bytes), 1);                      \
        }                                                                   \
    } while (0)

#define UB_NIO_PROFILE_COUNT(env, event, bytes)                             \
    do {                                                                    \
        if (ub_profile_enabled) {                                           \
            (*env)->UbSocketProfileRecord(env, (event), 0, (bytes), 1);     \
        }                                                                   \
    } while (0)

JNIEXPORT void JNICALL
Java_sun_nio_ch_FileDispatcherImpl_init(JNIEnv *env, jclass cl)
{
    // UB Matrix support: C wrapper profile events are detail-only.
    ub_profile_enabled =
        (*env)->UbSocketProfileMode(env) >= 2 ? JNI_TRUE : JNI_FALSE;

    int sp[2];
    if (socketpair(PF_UNIX, SOCK_STREAM, 0, sp) < 0) {
        JNU_ThrowIOExceptionWithLastError(env, "socketpair failed");
        return;
    }
    preCloseFD = sp[0];
    close(sp[1]);
}

// UB Matrix Support
static jlong
ub_socket_read_one(JNIEnv *env, jint fd, void *buf, jlong len,
                   jboolean convert_errors)
{
    jlong nread;
    char ub_msg[UB_SOCKET_PARSE_BATCH_BUF_LEN];
    int bytes_read;
    jlong result;
    jlong descriptor_recv_start;
    jlong descriptor_syscall_start;

    nread = (*env)->UbSocketRead(env, buf, fd, len);
    if (nread > 0) {
        UB_NIO_PROFILE_COUNT(env, UB_PROF_UB_FIRST_HIT, nread);
        return nread;
    }
    if (nread < 0) {
        return convert_errors == JNI_TRUE
            ? convertLongReturnVal(env, nread, JNI_TRUE) : nread;
    }
    // need to receive and parse new descriptors
    UB_NIO_PROFILE_COUNT(env, UB_PROF_UB_FIRST_MISS, 0);

    if ((*env)->IsUbSocketReady(env, fd) == JNI_FALSE) {
        bytes_read = read(fd, buf, len);
        result = convert_errors == JNI_TRUE
            ? convertLongReturnVal(env, bytes_read, JNI_TRUE) : bytes_read;
        return result;
    }

    UB_NIO_PROFILE_START(descriptor_recv_start);
    UB_NIO_PROFILE_START(descriptor_syscall_start);
    bytes_read = read(fd, ub_msg, UB_SOCKET_PARSE_BATCH_BUF_LEN);
    UB_NIO_PROFILE_END(env, UB_PROF_DESCRIPTOR_RECV_SYSCALL,
                       descriptor_syscall_start, bytes_read > 0 ? bytes_read : 0);
    if (bytes_read <= 0) {
        result = convert_errors == JNI_TRUE
            ? convertLongReturnVal(env, bytes_read, JNI_TRUE) : bytes_read;
        UB_NIO_PROFILE_END(env, UB_PROF_DESCRIPTOR_RECV_TOTAL,
                           descriptor_recv_start, 0);
        return result;
    }
    if ((*env)->UbSocketParse(env, fd, ub_msg, bytes_read) < 0) {
        result = convert_errors == JNI_TRUE
            ? convertLongReturnVal(env, -1, JNI_TRUE) : -1;
        UB_NIO_PROFILE_END(env, UB_PROF_DESCRIPTOR_RECV_TOTAL,
                           descriptor_recv_start, bytes_read);
        return result;
    }
    UB_NIO_PROFILE_END(env, UB_PROF_DESCRIPTOR_RECV_TOTAL,
                       descriptor_recv_start, bytes_read);
    nread = (*env)->UbSocketRead(env, buf, fd, len);
    if (nread < 0) {
        return convert_errors == JNI_TRUE
            ? convertLongReturnVal(env, nread, JNI_TRUE) : nread;
    }
    if (nread == 0 && (*env)->IsUbSocketReady(env, fd) == JNI_FALSE) {
        bytes_read = read(fd, buf, len);
        result = convert_errors == JNI_TRUE
            ? convertLongReturnVal(env, bytes_read, JNI_TRUE) : bytes_read;
        return result;
    }
    return nread;
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_FileDispatcherImpl_read0(JNIEnv *env, jclass clazz,
                             jobject fdo, jlong address, jint len)
{
    jint fd = fdval(env, fdo);
    void *buf = (void *)jlong_to_ptr(address);
    jint result;
    jlong total_start;
    jboolean ub_ready;

    UB_NIO_PROFILE_START(total_start);
    ub_ready = (*env)->IsUbSocketReady(env, fd);
    if (ub_ready == JNI_TRUE) {
        result = (jint)ub_socket_read_one(env, fd, buf, len, JNI_TRUE);
        UB_NIO_PROFILE_END(env, UB_PROF_NIO_READ_TOTAL, total_start,
                           result > 0 ? result : 0);
        return result;
    }

    result = read(fd, buf, len);
    result = convertReturnVal(env, result, JNI_TRUE);
    UB_NIO_PROFILE_END(env, UB_PROF_NIO_READ_TOTAL, total_start,
                       result > 0 ? result : 0);
    return result;
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_FileDispatcherImpl_pread0(JNIEnv *env, jclass clazz, jobject fdo,
                            jlong address, jint len, jlong offset)
{
    jint fd = fdval(env, fdo);
    void *buf = (void *)jlong_to_ptr(address);

    return convertReturnVal(env, pread64(fd, buf, len, offset), JNI_TRUE);
}

JNIEXPORT jlong JNICALL
Java_sun_nio_ch_FileDispatcherImpl_readv0(JNIEnv *env, jclass clazz,
                              jobject fdo, jlong address, jint len)
{
    jint fd = fdval(env, fdo);
    struct iovec *iov = (struct iovec *)jlong_to_ptr(address);
    jboolean ub_ready = (*env)->IsUbSocketReady(env, fd);
    if (ub_ready == JNI_TRUE) {
        jlong total = 0;
        int i;
        for (i = 0; i < len; i++) {
            jlong nread;
            if (iov[i].iov_len == 0) { continue; }
            nread = ub_socket_read_one(env, fd, iov[i].iov_base,
                                       (jlong)iov[i].iov_len,
                                       total == 0 ? JNI_TRUE : JNI_FALSE);
            if (nread <= 0) { return total > 0 ? total : nread; }
            total += nread;
            if ((size_t)nread < iov[i].iov_len) { return total; }
        }
        return total;
    }
    return convertLongReturnVal(env, readv(fd, iov, len), JNI_TRUE);
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_FileDispatcherImpl_write0(JNIEnv *env, jclass clazz,
                              jobject fdo, jlong address, jint len)
{
    jint fd = fdval(env, fdo);
    void *buf = (void *)jlong_to_ptr(address);
    jint nwrite = 0;
    jlong total_start;
    jboolean ub_ready_write;

    UB_NIO_PROFILE_START(total_start);
    ub_ready_write = (*env)->IsUbSocketReady(env, fd);
    if (ub_ready_write == JNI_TRUE) {
        nwrite = (*env)->UbSocketWrite(env, buf, fd, len);
    } else {
        nwrite = write(fd, buf, len);
    }

    nwrite = convertReturnVal(env, nwrite, JNI_FALSE);
    UB_NIO_PROFILE_END(env, UB_PROF_NIO_WRITE_TOTAL, total_start,
                       nwrite > 0 ? nwrite : 0);
    return nwrite;
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_FileDispatcherImpl_pwrite0(JNIEnv *env, jclass clazz, jobject fdo,
                            jlong address, jint len, jlong offset)
{
    jint fd = fdval(env, fdo);
    void *buf = (void *)jlong_to_ptr(address);

    return convertReturnVal(env, pwrite64(fd, buf, len, offset), JNI_FALSE);
}

JNIEXPORT jlong JNICALL
Java_sun_nio_ch_FileDispatcherImpl_writev0(JNIEnv *env, jclass clazz,
                                       jobject fdo, jlong address, jint len)
{
    jint fd = fdval(env, fdo);
    struct iovec *iov = (struct iovec *)jlong_to_ptr(address);
    jboolean ub_ready_write = (*env)->IsUbSocketReady(env, fd);
    if (ub_ready_write == JNI_TRUE) {
        jlong total = 0;
        int i;
        for (i = 0; i < len; i++) {
            jlong nwrite;
            if (iov[i].iov_len == 0) { continue; }
            nwrite = (*env)->UbSocketWrite(env, iov[i].iov_base, fd, (jlong)iov[i].iov_len);
            if (nwrite <= 0) {
                return total > 0 ? total : convertLongReturnVal(env, nwrite, JNI_FALSE);
            }
            total += nwrite;
            if ((size_t)nwrite < iov[i].iov_len) { return total; }
        }
        return total;
    }
    return convertLongReturnVal(env, writev(fd, iov, len), JNI_FALSE);
}

static jlong
handle(JNIEnv *env, jlong rv, char *msg)
{
    if (rv >= 0)
        return rv;
    if (errno == EINTR)
        return IOS_INTERRUPTED;
    JNU_ThrowIOExceptionWithLastError(env, msg);
    return IOS_THROWN;
}

JNIEXPORT jlong JNICALL
Java_sun_nio_ch_FileDispatcherImpl_seek0(JNIEnv *env, jclass clazz,
                                         jobject fdo, jlong offset)
{
    jint fd = fdval(env, fdo);
    off64_t result;
    if (offset < 0) {
        result = lseek64(fd, 0, SEEK_CUR);
    } else {
        result = lseek64(fd, offset, SEEK_SET);
    }
    return handle(env, (jlong)result, "lseek64 failed");
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_FileDispatcherImpl_force0(JNIEnv *env, jobject this,
                                          jobject fdo, jboolean md)
{
    jint fd = fdval(env, fdo);
    int result = 0;

    if (md == JNI_FALSE) {
        result = fdatasync(fd);
    } else {
#ifdef _AIX
        /* On AIX, calling fsync on a file descriptor that is opened only for
         * reading results in an error ("EBADF: The FileDescriptor parameter is
         * not a valid file descriptor open for writing.").
         * However, at this point it is not possibly anymore to read the
         * 'writable' attribute of the corresponding file channel so we have to
         * use 'fcntl'.
         */
        int getfl = fcntl(fd, F_GETFL);
        if (getfl >= 0 && (getfl & O_ACCMODE) == O_RDONLY) {
            return 0;
        }
#endif
        result = fsync(fd);
    }
    return handle(env, result, "Force failed");
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_FileDispatcherImpl_truncate0(JNIEnv *env, jobject this,
                                             jobject fdo, jlong size)
{
    return handle(env,
                  ftruncate64(fdval(env, fdo), size),
                  "Truncation failed");
}

JNIEXPORT jlong JNICALL
Java_sun_nio_ch_FileDispatcherImpl_size0(JNIEnv *env, jobject this, jobject fdo)
{
    jint fd = fdval(env, fdo);
    struct stat64 fbuf;

    if (fstat64(fd, &fbuf) < 0)
        return handle(env, -1, "Size failed");

#ifdef BLKGETSIZE64
    if (S_ISBLK(fbuf.st_mode)) {
        uint64_t size;
        if (ioctl(fd, BLKGETSIZE64, &size) < 0)
            return handle(env, -1, "Size failed");
        return (jlong)size;
    }
#endif

    return fbuf.st_size;
}

JNIEXPORT jint JNICALL
Java_sun_nio_ch_FileDispatcherImpl_lock0(JNIEnv *env, jobject this, jobject fdo,
                                      jboolean block, jlong pos, jlong size,
                                      jboolean shared)
{
    jint fd = fdval(env, fdo);
    jint lockResult = 0;
    int cmd = 0;
    struct flock64 fl;

    fl.l_whence = SEEK_SET;
    if (size == (jlong)java_lang_Long_MAX_VALUE) {
        fl.l_len = (off64_t)0;
    } else {
        fl.l_len = (off64_t)size;
    }
    fl.l_start = (off64_t)pos;
    if (shared == JNI_TRUE) {
        fl.l_type = F_RDLCK;
    } else {
        fl.l_type = F_WRLCK;
    }
    if (block == JNI_TRUE) {
        cmd = F_SETLKW64;
    } else {
        cmd = F_SETLK64;
    }
    lockResult = fcntl(fd, cmd, &fl);
    if (lockResult < 0) {
        if ((cmd == F_SETLK64) && (errno == EAGAIN || errno == EACCES))
            return sun_nio_ch_FileDispatcherImpl_NO_LOCK;
        if (errno == EINTR)
            return sun_nio_ch_FileDispatcherImpl_INTERRUPTED;
        JNU_ThrowIOExceptionWithLastError(env, "Lock failed");
    }
    return 0;
}

JNIEXPORT void JNICALL
Java_sun_nio_ch_FileDispatcherImpl_release0(JNIEnv *env, jobject this,
                                         jobject fdo, jlong pos, jlong size)
{
    jint fd = fdval(env, fdo);
    jint lockResult = 0;
    struct flock64 fl;
    int cmd = F_SETLK64;

    fl.l_whence = SEEK_SET;
    if (size == (jlong)java_lang_Long_MAX_VALUE) {
        fl.l_len = (off64_t)0;
    } else {
        fl.l_len = (off64_t)size;
    }
    fl.l_start = (off64_t)pos;
    fl.l_type = F_UNLCK;
    lockResult = fcntl(fd, cmd, &fl);
    if (lockResult < 0) {
        JNU_ThrowIOExceptionWithLastError(env, "Release failed");
    }
}


static void closeFileDescriptor(JNIEnv *env, int fd) {
    if (fd != -1) {
        int result = close(fd);
        if (result < 0)
            JNU_ThrowIOExceptionWithLastError(env, "Close failed");
    }
}

static jboolean isSocketFD(int fd) {
    int sotype = 0;
    socklen_t arglen = sizeof(sotype);
    return getsockopt(fd, SOL_SOCKET, SO_TYPE, (void *)&sotype, &arglen) == 0;
}

JNIEXPORT void JNICALL
Java_sun_nio_ch_FileDispatcherImpl_close0(JNIEnv *env, jclass clazz, jobject fdo)
{
    jint fd = fdval(env, fdo);
    if (isSocketFD(fd)) {
        (*env)->UbSocketClose(env, fd);
    }
    closeFileDescriptor(env, fd);
}

JNIEXPORT void JNICALL
Java_sun_nio_ch_FileDispatcherImpl_preClose0(JNIEnv *env, jclass clazz, jobject fdo)
{
    jint fd = fdval(env, fdo);
    if (preCloseFD >= 0) {
        if (dup2(preCloseFD, fd) < 0)
            JNU_ThrowIOExceptionWithLastError(env, "dup2 failed");
    }
}

JNIEXPORT void JNICALL
Java_sun_nio_ch_FileDispatcherImpl_closeIntFD(JNIEnv *env, jclass clazz, jint fd)
{
    closeFileDescriptor(env, fd);
}
