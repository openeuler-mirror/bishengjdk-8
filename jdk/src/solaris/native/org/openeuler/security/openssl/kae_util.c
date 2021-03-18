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

#include <openssl/evp.h>
#include "kae_util.h"
#include "kae_exception.h"


BIGNUM* KAE_GetBigNumFromByteArray(JNIEnv* env, jbyteArray byteArray) {
    if (byteArray == NULL) {
        KAE_ThrowNullPointerException(env, "KAE_GetBigNumFromByteArray byteArray is null");
        return NULL;
    }

    jsize len = (*env)->GetArrayLength(env, byteArray);
    if (len == 0) {
        KAE_ThrowRuntimeException(env, "KAE_GetBigNumFromByteArray byteArray is empty");
        return NULL;
    }

    BIGNUM* bn = BN_new();
    if (bn == NULL) {
        KAE_ThrowFromOpenssl(env, "BN_new", KAE_ThrowRuntimeException);
        return NULL;
    }

    jbyte* bytes = (*env)->GetByteArrayElements(env, byteArray, NULL);
    if (bytes == NULL) {
        KAE_ThrowNullPointerException(env,"GetByteArrayElements failed");
        goto error;
    }
    BIGNUM* result = BN_bin2bn((const unsigned char*) bytes, len, bn);
    (*env)->ReleaseByteArrayElements(env, byteArray, bytes, 0);
    if (result == NULL) {
        KAE_ThrowFromOpenssl(env, "BN_bin2bn", KAE_ThrowRuntimeException);
        goto error;
    }
    return bn;

error:
    BN_free(bn);
    return NULL;
}

void KAE_ReleaseBigNumFromByteArray(BIGNUM* bn) {
    if (bn != NULL) {
        BN_free(bn);
    }
}

jbyteArray KAE_GetByteArrayFromBigNum(JNIEnv* env, const BIGNUM* bn, const char* sourceName) {
    if (bn == NULL) {
        return NULL;
    }
    // bn size need plus 1,  for example 65535 , BN_num_bytes return 2
    int bnSize = BN_num_bytes(bn);
    if (bnSize <= 0) {
        return NULL;
    }
    bnSize += 1;
    jbyteArray javaBytes = (*env)->NewByteArray(env, bnSize);
    if (javaBytes == NULL) {
        KAE_ThrowOOMException(env, "new byte array failed");
        return NULL;
    }
    jbyte* bytes = (*env)->GetByteArrayElements(env, javaBytes, NULL);
    if (bytes == NULL) {
        KAE_ThrowNullPointerException(env,"GetByteArrayElements failed");
        return NULL;
    }
    unsigned char* tmp = (unsigned char*) bytes;
    if (BN_bn2bin(bn, tmp + 1) <= 0) {
        KAE_ThrowFromOpenssl(env, "BN_bn2bin", KAE_ThrowRuntimeException);
        javaBytes = NULL;
        goto cleanup;
    }
    (*env)->SetByteArrayRegion(env, javaBytes, 0, bnSize, bytes);

cleanup:
    (*env)->ReleaseByteArrayElements(env, javaBytes, bytes, 0);
    return javaBytes;
}
