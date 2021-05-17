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

#include <memory.h>
#include <openssl/objects.h>
#include <openssl/ec.h>
#include "kae_log.h"
#include "kae_exception.h"
#include "kae_util.h"
#include "org_openeuler_security_openssl_KAEECDHKeyAgreement.h"

static void FreeGenerateSecretParam(BIGNUM* s, BIGNUM* wX, BIGNUM* wY,
    EC_POINT* pub, EC_KEY* eckey, EC_GROUP* group, unsigned char* shareKey)
{
    KAE_ReleaseBigNumFromByteArray(s);
    KAE_ReleaseBigNumFromByteArray(wX);
    KAE_ReleaseBigNumFromByteArray(wY);
    if (pub != NULL) {
        EC_POINT_free(pub);
    }
    if (eckey != NULL) {
        EC_KEY_free(eckey);
    }
    if (group != NULL) {
        EC_GROUP_free(group);
    }
    if (shareKey != NULL) {
        free(shareKey);
    }
}

/*
 * Class:     org_openeuler_security_openssl_KAEECDHKeyAgreement
 * Method:    nativeGenerateSecret
 * Signature: (Ljava/lang/String;[B[B[B)[B
 */
JNIEXPORT jbyteArray JNICALL Java_org_openeuler_security_openssl_KAEECDHKeyAgreement_nativeGenerateSecret
    (JNIEnv* env, jclass cls, jstring curveName, jbyteArray wXArr, jbyteArray wYArr, jbyteArray sArr)
{
    EC_GROUP* group = NULL;
    EC_KEY* eckey = NULL;
    BIGNUM* wX = NULL;
    BIGNUM* wY = NULL;
    BIGNUM* s = NULL;
    EC_POINT* pub = NULL;
    jbyteArray javaBytes = NULL;
    unsigned char* shareKey = NULL;
    const char *curve = (*env)->GetStringUTFChars(env, curveName, 0);
    int nid = OBJ_sn2nid(curve);
    (*env)->ReleaseStringUTFChars(env, curveName, curve);
    if ((nid == NID_undef) || (group = EC_GROUP_new_by_curve_name(nid)) == NULL) {
        goto cleanup;
    }
    if ((s = KAE_GetBigNumFromByteArray(env, sArr)) == NULL || (wX = KAE_GetBigNumFromByteArray(env, wXArr)) == NULL
        || (wY = KAE_GetBigNumFromByteArray(env, wYArr)) == NULL) {
        KAE_ThrowOOMException(env, "failed to allocate BN_new");
        goto cleanup;
    }
    if ((eckey = EC_KEY_new()) == NULL || !EC_KEY_set_group(eckey, group)) {
        goto cleanup;
    }
    if ((pub = EC_POINT_new(group)) == NULL) {
        goto cleanup;
    }
    if (!EC_POINT_set_affine_coordinates_GFp(group, pub, wX, wY, NULL)) {
        goto cleanup;
    }
    if (!EC_KEY_set_public_key(eckey, pub) || !EC_KEY_set_private_key(eckey, s)) {
        goto cleanup;
    }

    // Get the length of secret key, in bytes.
    int expectSecretLen = (EC_GROUP_get_degree(group) + 7) / 8;
    if ((shareKey = malloc(expectSecretLen)) == NULL) {
        KAE_ThrowOOMException(env, "malloc error");
        goto cleanup;
    }
    memset(shareKey, 0, expectSecretLen);

    // Perform ecdh keyagreement.
    if (ECDH_compute_key(shareKey, expectSecretLen, pub, eckey, NULL) != expectSecretLen) {
        goto cleanup;
    }

    if ((javaBytes = (*env)->NewByteArray(env, expectSecretLen)) == NULL) {
        goto cleanup;
    }
    (*env)->SetByteArrayRegion(env, javaBytes, 0, expectSecretLen, (jbyte*)shareKey);
    FreeGenerateSecretParam(s, wX, wY, pub, eckey, group, shareKey);
    return javaBytes;

cleanup:
    FreeGenerateSecretParam(s, wX, wY, pub, eckey, group, shareKey);
    return NULL;
}
