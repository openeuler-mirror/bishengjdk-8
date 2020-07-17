/*
 * Copyright (c) 2016, Oracle and/or its affiliates. All rights reserved.
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

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "jni.h"

#include "sun_nio_fs_UnixConstants.h"

#define INIT_CONST(ENV, CLS, VAL) INIT_CONST_NAME(ENV, CLS, VAL, p ## VAL)

#define INIT_CONST_NAME(ENV, CLS, VAL, NAME)                                \
{                                                                           \
    jfieldID fID = (*(ENV))->GetStaticFieldID((ENV), (CLS), #NAME, "I");    \
    if (fID != 0) {                                                          \
        (*(ENV))->SetStaticIntField((ENV), (CLS), fID, (VAL));              \
    }                                                                       \
}                                                                           \

/**
 * Initialization of the UnixConstants fields:
 * file open flags, modes and error codes
 */
JNIEXPORT void JNICALL
Java_sun_nio_fs_UnixConstants_init(JNIEnv* env, jclass cls) {
    // open flags
    INIT_CONST(env, cls, O_RDONLY);
    INIT_CONST(env, cls, O_WRONLY);
    INIT_CONST(env, cls, O_RDWR);
    INIT_CONST(env, cls, O_APPEND);
    INIT_CONST(env, cls, O_CREAT);
    INIT_CONST(env, cls, O_EXCL);
    INIT_CONST(env, cls, O_TRUNC);
    INIT_CONST(env, cls, O_SYNC);
    INIT_CONST(env, cls, O_DSYNC);
    INIT_CONST(env, cls, O_NOFOLLOW);

    // mode masks
    INIT_CONST(env, cls, S_IRUSR);
    INIT_CONST(env, cls, S_IWUSR);
    INIT_CONST(env, cls, S_IXUSR);
    INIT_CONST(env, cls, S_IRGRP);
    INIT_CONST(env, cls, S_IWGRP);
    INIT_CONST(env, cls, S_IXGRP);
    INIT_CONST(env, cls, S_IROTH);
    INIT_CONST(env, cls, S_IWOTH);
    INIT_CONST(env, cls, S_IXOTH);
    INIT_CONST(env, cls, S_IFMT);
    INIT_CONST(env, cls, S_IFREG);
    INIT_CONST(env, cls, S_IFDIR);
    INIT_CONST(env, cls, S_IFLNK);
    INIT_CONST(env, cls, S_IFCHR);
    INIT_CONST(env, cls, S_IFBLK);
    INIT_CONST(env, cls, S_IFIFO);
    INIT_CONST_NAME(env, cls, (S_IRUSR|S_IWUSR|S_IXUSR|S_IRGRP|S_IWGRP|S_IXGRP|S_IROTH|S_IWOTH|S_IXOTH), pS_IAMB);

    // access modes
    INIT_CONST(env, cls, R_OK);
    INIT_CONST(env, cls, W_OK);
    INIT_CONST(env, cls, X_OK);
    INIT_CONST(env, cls, F_OK);

    // errors
    INIT_CONST(env, cls, ENOENT);
    INIT_CONST(env, cls, EACCES);
    INIT_CONST(env, cls, EEXIST);
    INIT_CONST(env, cls, ENOTDIR);
    INIT_CONST(env, cls, EINVAL);
    INIT_CONST(env, cls, EXDEV);
    INIT_CONST(env, cls, EISDIR);
    INIT_CONST(env, cls, ENOTEMPTY);
    INIT_CONST(env, cls, ENOSPC);
    INIT_CONST(env, cls, EAGAIN);
    INIT_CONST(env, cls, ENOSYS);
    INIT_CONST(env, cls, ELOOP);
    INIT_CONST(env, cls, EROFS);
    INIT_CONST(env, cls, ERANGE);
    INIT_CONST(env, cls, EMFILE);

#if defined(ENODATA)
    INIT_CONST(env, cls, ENODATA);
#endif

#if defined(AT_SYMLINK_NOFOLLOW) && defined(AT_REMOVEDIR)
    INIT_CONST(env, cls, AT_SYMLINK_NOFOLLOW);
    INIT_CONST(env, cls, AT_REMOVEDIR);
#endif

}
