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
#include "kae_log.h"
#include "ssl_utils.h"
#include "org_openeuler_security_openssl_KAEProvider.h"

/*
 * Class:     Java_org_openeuler_security_openssl_KAEProvider
 * Method:    initOpenssl
 * Signature: ()V
 */
JNIEXPORT int JNICALL Java_org_openeuler_security_openssl_KAEProvider_initOpenssl(JNIEnv *env, jclass cls,
    jint useOpensslVersion, jstring engineId, jbooleanArray algorithmKaeFlags)
{
    // Load openssl functions by dlsym(), according to current libssl.so file version.
    jboolean init_result = SSL_UTILS_func_ptr_init(env, useOpensslVersion);
    if (!init_result) {
        return -1;
    }
    // Change from macro, SSL_load_error_strings is a macro in openssl 1 and 3.
    SSL_UTILS_SSL_load_error_strings();
    SSL_UTILS_ERR_load_BIO_strings();
    // Change from macro, OpenSSL_add_all_algorithms ia a macro, defined by OPENSSL_LOAD_CONF value.
    SSL_UTILS_OpenSSL_add_all_algorithms();

    // check if KaeEngine holder is already set
    ENGINE* e = GetKaeEngine();
    if (e != NULL) {
        SSL_UTILS_ENGINE_free(e);
        e = NULL;
    }

    // determine whether KAE is loaded successfully
    const char* id = (*env)->GetStringUTFChars(env, engineId, 0);
    e = SSL_UTILS_ENGINE_by_id(id);
    (*env)->ReleaseStringUTFChars(env, engineId, id);
    if (e == NULL) {
        KAE_ThrowFromOpenssl(env, "ENGINE_by_id", KAE_ThrowRuntimeException);
        return -1;
    }
    SetKaeEngine(e);

    // initialize the engine for each algorithm
    initEngines(env, algorithmKaeFlags);

    return get_sslVersion();
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