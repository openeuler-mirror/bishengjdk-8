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
#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/engine.h>
#include <openssl/dh.h>
#include <stdio.h>
#include "kae_util.h"
#include "kae_exception.h"
#include "kae_log.h"
#include "org_openeuler_security_openssl_KAEDHKeyAgreement.h"


/*
 * Class:     org_openeuler_security_openssl_KAEDHKeyAgreement
 * Method:    nativeComputeKey
 */
JNIEXPORT jbyteArray JNICALL Java_org_openeuler_security_openssl_KAEDHKeyAgreement_nativeComputeKey(JNIEnv* env,
                                 jobject obj, jbyteArray y, jbyteArray x, jbyteArray p, jbyteArray g, jint pSize) {

    KAE_TRACE("Java_org_openeuler_security_openssl_KAEDHKeyAgreement_nativeComputeKey start.");

    DH* dh = NULL;
    BIGNUM* y_bn = NULL;
    BIGNUM* x_bn = NULL;
    BIGNUM* p_bn = NULL;
    BIGNUM* g_bn = NULL;
    BIGNUM* computeKeyRetBn = NULL;
    int computekeyLength = 0;
    unsigned char* secret = NULL;
    jbyteArray retByteArray = NULL;
    static ENGINE* kaeEngine = NULL;
    kaeEngine = (kaeEngine == NULL) ? GetKaeEngine() : kaeEngine;

    // bits to Bytes
    int pSizeInByte = (pSize +7) >> 3;

    if ((secret = (unsigned char*)malloc(pSizeInByte)) == NULL) {
        KAE_ThrowOOMException(env, "malloc secret failed.");
        goto cleanup;
    }
    memset(secret, 0, pSizeInByte);

    if ((dh = DH_new_method(kaeEngine)) == NULL) {
        KAE_ThrowOOMException(env, "Allocate DH failed in nativeComputeKey.");
        goto cleanup;
    }

    if ((y_bn = KAE_GetBigNumFromByteArray(env, y)) == NULL) {
        KAE_ThrowOOMException(env, "Convert y to BIGNUM failed.");
        goto cleanup;
    }

    if ((x_bn = KAE_GetBigNumFromByteArray(env, x)) == NULL) {
        KAE_ThrowOOMException(env, "Convert x to BIGNUM failed.");
        goto cleanup;
    }

    if ((p_bn = KAE_GetBigNumFromByteArray(env, p)) == NULL) {
        KAE_ThrowOOMException(env, "Convert p to BIGNUM failed.");
        goto cleanup;
    }

    if ((g_bn = KAE_GetBigNumFromByteArray(env, g)) == NULL) {
        KAE_ThrowOOMException(env, "Convert g to BIGNUM failed.");
        goto cleanup;
    }

    if ((computeKeyRetBn = BN_new()) == NULL) {
        KAE_ThrowOOMException(env, "Allocate BN failed.");
        goto cleanup;
    }

    if (!DH_set0_pqg(dh, BN_dup(p_bn), NULL, BN_dup(g_bn))) {
        KAE_ThrowRuntimeException(env, "DH_set0_pqg failed.");
        goto cleanup;
    }

    if (!DH_set0_key(dh,  NULL, BN_dup(x_bn))) {
        KAE_ThrowRuntimeException(env, "DH_set0_key failed.");
        goto cleanup;
    }

    computekeyLength = DH_compute_key(secret, y_bn, dh);

    if (computekeyLength  <= 0 ) {
        KAE_ThrowRuntimeException(env, "DH_compute_key failed.");
        goto cleanup;
    }

    BN_bin2bn(secret, computekeyLength, computeKeyRetBn);

    retByteArray = KAE_GetByteArrayFromBigNum(env, computeKeyRetBn);
    if (retByteArray == NULL) {
        KAE_ThrowRuntimeException(env, "GetByteArrayFromBigNum failed in nativeComputeKey.");
        goto cleanup;
    }
    KAE_TRACE("Java_org_openeuler_security_openssl_KAEDHKeyAgreement_nativeGenerateSecret finished!");

cleanup:
    if (dh != NULL)
        DH_free(dh);
    if (y_bn != NULL)
        KAE_ReleaseBigNumFromByteArray(y_bn);
    if (x_bn != NULL)
        KAE_ReleaseBigNumFromByteArray(x_bn);
    if (p_bn != NULL)
        KAE_ReleaseBigNumFromByteArray(p_bn);
    if (g_bn != NULL)
        KAE_ReleaseBigNumFromByteArray(g_bn);
    if (secret != NULL)
       free(secret);
    if (computeKeyRetBn != NULL)
       BN_free(computeKeyRetBn);

    return retByteArray;
}
