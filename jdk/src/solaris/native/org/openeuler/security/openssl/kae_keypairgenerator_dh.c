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
#include <openssl/dh.h>
#include <stdio.h>
#include "kae_util.h"
#include "kae_log.h"
#include "org_openeuler_security_openssl_KAEDHKeyPairGenerator.h"
#include "kae_exception.h"


/*
* Class:     org_openeuler_security_openssl_KAEDHKeyPairGenerator
* Method:    nativeGenerateKeyPair
* Signature: ([B[BI)[[B
*/

JNIEXPORT jobjectArray JNICALL Java_org_openeuler_security_openssl_KAEDHKeyPairGenerator_nativeGenerateKeyPair
    (JNIEnv* env, jclass cls, jbyteArray p, jbyteArray g,  jint lSize)
{
    DH* dh = NULL;
    BIGNUM* p_bn = NULL;
    BIGNUM* g_bn = NULL;
    const BIGNUM* pri_key_bn = NULL;
    const BIGNUM* pub_key_bn = NULL;
    jclass byteArrayClass = NULL;
    jobjectArray keys = NULL;
    jbyteArray pri_key = NULL;
    jbyteArray pub_key = NULL;
    static ENGINE* kaeEngine = NULL;
    kaeEngine = (kaeEngine == NULL) ? GetKaeEngine() : kaeEngine;

    KAE_TRACE("Java_org_openeuler_security_openssl_KAEDHKeyPairGenerator_nativeGenerateKeyPair start !");

    if ((dh = DH_new_method(kaeEngine)) == NULL) {
        KAE_ThrowOOMException(env, "Allocate DH failed in nativeGenerateKeyPair!");
        goto cleanup;
    }

    if ((p_bn = KAE_GetBigNumFromByteArray(env, p)) == NULL) {
        KAE_ThrowOOMException(env, "Allocate p_bn failed in nativeGenerateKeyPair!");
        goto cleanup;
    }

    if ((g_bn = KAE_GetBigNumFromByteArray(env, g)) == NULL) {
        KAE_ThrowOOMException(env, "Allocate g_bn failed in nativeGenerateKeyPair!");
        goto cleanup;
    }

    if (!DH_set0_pqg(dh, BN_dup(p_bn), NULL, BN_dup(g_bn))) {
        KAE_ThrowRuntimeException(env, "DH_set0_pqg failed in nativeGenerateKeyPair.");
        goto cleanup;
    }

    // Return value is fixed to 1, nothing to check.
    DH_set_length(dh, lSize);

    if (!DH_generate_key(dh)) {
        KAE_ThrowInvalidAlgorithmParameterException(env, "DH generate key failed in nativeGenerateKeyPair.");
        goto cleanup;
    }

    if ((byteArrayClass = (*env)->FindClass(env, "[B")) == NULL) {
        KAE_ThrowClassNotFoundException(env, "Class byte[] not found.");
        goto cleanup;
    }

    if ((keys = (*env)->NewObjectArray(env, 2, byteArrayClass, NULL)) == NULL) {
        KAE_ThrowOOMException(env, "Allocate ByteArray failed in nativeGenerateKeyPair!");
        goto cleanup;
    }

    // Return the ptr of private  key in dh.
    pri_key_bn = DH_get0_priv_key(dh);
    pub_key_bn = DH_get0_pub_key(dh);

    pub_key = KAE_GetByteArrayFromBigNum(env, pub_key_bn);
    if (pub_key == NULL) {
        KAE_ThrowOOMException(env, "PublicKey allocate failed in nativeGenerateKeyPair.");
        goto cleanup;
    }

    pri_key = KAE_GetByteArrayFromBigNum(env, pri_key_bn);
    if (pri_key == NULL) {
        KAE_ThrowRuntimeException(env, "GetByteArrayFromBigNum failed in nativeGenerateKeyPair.");
        goto cleanup;
    }

    (*env)->SetObjectArrayElement(env, keys, 0, pub_key);
    (*env)->SetObjectArrayElement(env, keys, 1, pri_key);

    KAE_TRACE("Java_org_openeuler_security_openssl_KAEDHKeyPairGenerator_nativeGenerateKeyPair finished !");

cleanup:
    if (dh != NULL)
        DH_free(dh);
    if (p_bn != NULL)
        KAE_ReleaseBigNumFromByteArray(p_bn);
    if (g_bn != NULL)
        KAE_ReleaseBigNumFromByteArray(g_bn);
    if (byteArrayClass != NULL)
        (*env)->DeleteLocalRef(env, byteArrayClass);
    if (pub_key != NULL)
        (*env)->DeleteLocalRef(env, pub_key);
    if (pri_key != NULL)
        (*env)->DeleteLocalRef(env, pri_key);

    return keys;
}
