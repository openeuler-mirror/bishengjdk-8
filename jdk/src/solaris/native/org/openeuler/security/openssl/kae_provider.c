/*
 * Copyright (c) 2021, Huawei Technologies Co., Ltd. All rights reserved.
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

#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/engine.h>
#include <dlfcn.h>
#include "kae_exception.h"
#include "kae_util.h"
#include "org_openeuler_security_openssl_KAEProvider.h"

#define KAE_OPENSSL_LIBRARY "libcrypto.so"

/*
 * Class:     Java_org_openeuler_security_openssl_KAEProvider
 * Method:    initOpenssl
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_org_openeuler_security_openssl_KAEProvider_initOpenssl
        (JNIEnv *env, jclass cls, jboolean useGlobalMode, jstring engineId, jbooleanArray algorithmKaeFlags) {
    SSL_load_error_strings();
    ERR_load_BIO_strings();
    OpenSSL_add_all_algorithms();

    /*
     * If the same shared object is opened again with dlopen(), the same object handle is returned.
     * The dynamic linker maintains reference counts for object handles.
     * An object that was previously opened with RTLD_LOCAL can be promoted to RTLD_GLOBAL in a subsequent dlopen().
     *
     * RTLD_GLOBAL
	 *     The symbols defined by this shared object will be made
	 *     available for symbol resolution of subsequently loaded
	 *     shared objects.
     * RTLD_LOCAL
	 *     This is the converse of RTLD_GLOBAL, and the default if
	 *     neither flag is specified.  Symbols defined in this shared
	 *     object are not made available to resolve references in
	 *     subsequently loaded shared objects.
     * For more information see https://man7.org/linux/man-pages/man3/dlopen.3.html.
     */
    if (useGlobalMode) {
        char msg[1024];
        void *handle = NULL;
        // Promote the flags of the loaded libcrypto.so library from RTLD_LOCAL to RTLD_GLOBAL
        handle = dlopen(KAE_OPENSSL_LIBRARY, RTLD_LAZY | RTLD_GLOBAL);
        if (handle == NULL) {
            snprintf(msg, sizeof(msg), "Cannot load %s (%s)!", KAE_OPENSSL_LIBRARY,  dlerror());
            KAE_ThrowByName(env, "java/lang/UnsatisfiedLinkError", msg);
            return;
        }
        dlclose(handle);
    }

    // check if KaeEngine holder is already set
    ENGINE* e = GetKaeEngine();
    if (e != NULL) {
        ENGINE_free(e);
        e = NULL;
    }

    // determine whether KAE is loaded successfully
    const char* id = (*env)->GetStringUTFChars(env, engineId, 0);
    e = ENGINE_by_id(id);
    (*env)->ReleaseStringUTFChars(env, engineId, id);
    if (e == NULL) {
        KAE_ThrowFromOpenssl(env, "ENGINE_by_id", KAE_ThrowRuntimeException);
        return;
    }
    SetKaeEngine(e);

    // initialize the engine for each algorithm
    initEngines(env, algorithmKaeFlags);
}

/*
 * Class:     Java_org_openeuler_security_openssl_KAEProvider
 * Method:    getEngineFlags
 * Signature: ()V
 */
JNIEXPORT jbooleanArray JNICALL Java_org_openeuler_security_openssl_KAEProvider_getEngineFlags
        (JNIEnv *env, jclass cls) {
    return getEngineFlags(env);
}