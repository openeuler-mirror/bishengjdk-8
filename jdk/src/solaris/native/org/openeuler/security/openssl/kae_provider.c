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
#include "kae_exception.h"
#include "org_openeuler_security_openssl_KAEProvider.h"
/*
 * Class:     org_openeuler_security_openssl_WdProvider
 * Method:    initOpenssl
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_org_openeuler_security_openssl_KAEProvider_initOpenssl
        (JNIEnv *env, jclass cls) {
    SSL_load_error_strings();
    ERR_load_BIO_strings();
    OpenSSL_add_all_algorithms();

    // determine whether KAE is loaded successfully
    ENGINE *e = ENGINE_by_id("kae");
    if (e == NULL) {
        KAE_ThrowRuntimeException(env, "kae engine not found");
        return;
    }
    ENGINE_free(e);
}
