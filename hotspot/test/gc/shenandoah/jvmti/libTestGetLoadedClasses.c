/*
 * Copyright (c) 2019, Red Hat, Inc. All rights reserved.
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

#include <stdio.h>
#include <string.h>
#include "jvmti.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef JNI_ENV_ARG

#ifdef __cplusplus
#define JNI_ENV_ARG(x, y) y
#define JNI_ENV_PTR(x) x
#else
#define JNI_ENV_ARG(x,y) x, y
#define JNI_ENV_PTR(x) (*x)
#endif

#endif

static const char *EXC_CNAME = "java/lang/Exception";

static jvmtiEnv *jvmti = NULL;

static jint Agent_Initialize(JavaVM *jvm, char *options, void *reserved);

JNIEXPORT
jint JNICALL Agent_OnLoad(JavaVM *jvm, char *options, void *reserved) {
    return Agent_Initialize(jvm, options, reserved);
}

JNIEXPORT
jint JNICALL Agent_OnAttach(JavaVM *jvm, char *options, void *reserved) {
    return Agent_Initialize(jvm, options, reserved);
}

JNIEXPORT
jint JNICALL JNI_OnLoad(JavaVM *jvm, void *reserved) {
    return JNI_VERSION_1_8;
}

static
jint Agent_Initialize(JavaVM *jvm, char *options, void *reserved) {
    jvmtiCapabilities capabilities;
    jint res = JNI_ENV_PTR(jvm)->GetEnv(JNI_ENV_ARG(jvm, (void **) &jvmti),
                                        JVMTI_VERSION);
    if (res != JNI_OK || jvmti == NULL) {
        printf("    Error: wrong result of a valid call to GetEnv!\n");
        return JNI_ERR;
    }

    (void)memset(&capabilities, 0, sizeof(capabilities));
    capabilities.can_tag_objects = 1;
    capabilities.can_generate_garbage_collection_events = 1;
    (*jvmti)->AddCapabilities(jvmti, &capabilities);

    return JNI_OK;
}

static
void throw_exc(JNIEnv *env, char *msg) {
    jclass exc_class = JNI_ENV_PTR(env)->FindClass(JNI_ENV_ARG(env, EXC_CNAME));
    jint rt = JNI_OK;

    if (exc_class == NULL) {
        printf("throw_exc: Error in FindClass(env, %s)\n", EXC_CNAME);
        return;
    }
    rt = JNI_ENV_PTR(env)->ThrowNew(JNI_ENV_ARG(env, exc_class), msg);
    if (rt == JNI_ERR) {
        printf("throw_exc: Error in JNI ThrowNew(env, %s)\n", msg);
    }
}

JNIEXPORT jint JNICALL
Java_TestGetLoadedClasses_getLoadedClasses(JNIEnv *env, jclass cls) {
    jint totalCount = 0;
    jclass* classes;
    if (jvmti == NULL) {
        throw_exc(env, "JVMTI client was not properly loaded!\n");
        return 0;
    }

    (*jvmti)->GetLoadedClasses(jvmti, &totalCount, &classes);
    (*jvmti)->Deallocate(jvmti, (unsigned char*)classes);
    return totalCount;
}

#ifdef __cplusplus
}
#endif
