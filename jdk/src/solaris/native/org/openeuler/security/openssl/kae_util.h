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

#ifndef KAE_UTIL_H
#define KAE_UTIL_H

#include <openssl/bn.h>
#include <jni.h>

typedef enum {
    MD5_INDEX,
    SHA256_INDEX,
    SHA384_INDEX,
    SM3_INDEX,
    AES_128_ECB_INDEX,
    AES_128_CBC_INDEX,
    AES_128_CTR_INDEX,
    AES_128_GCM_INDEX,
    AES_192_ECB_INDEX,
    AES_192_CBC_INDEX,
    AES_192_CTR_INDEX,
    AES_192_GCM_INDEX,
    AES_256_ECB_INDEX,
    AES_256_CBC_INDEX,
    AES_256_CTR_INDEX,
    AES_256_GCM_INDEX,
    SM4_ECB_INDEX,
    SM4_CBC_INDEX,
    SM4_CTR_INDEX,
    SM4_OFB_INDEX,
    HMAC_MD5_INDEX,
    HMAC_SHA1_INDEX,
    HMAC_SHA224_INDEX,
    HMAC_SHA256_INDEX,
    HMAC_SHA384_INDEX,
    HMAC_SHA512_INDEX,
    RSA_INDEX,
    DH_INDEX,
    EC_INDEX
} AlgorithmIndex;

typedef struct {
    AlgorithmIndex algorithmIndex;
    const char* algorithmName;
} KAEAlgorithm;

/* jbyteArray convert to BIGNUM */
BIGNUM* KAE_GetBigNumFromByteArray(JNIEnv* env, jbyteArray byteArray);

/* release BIGNUM allocat from */
void KAE_ReleaseBigNumFromByteArray(BIGNUM* bn);

/* BIGNUM convert to jbyteArray */
jbyteArray KAE_GetByteArrayFromBigNum(JNIEnv* env, const BIGNUM* bn);

void SetKaeEngine(ENGINE* engine);

ENGINE* GetKaeEngine();

void initEngines(JNIEnv* env, jbooleanArray algorithmKaeFlags);

jbooleanArray getEngineFlags(JNIEnv* env);

ENGINE* GetEngineByAlgorithmIndex(AlgorithmIndex algorithmIndex);

ENGINE* GetHmacEngineByAlgorithmName(const char* algorithmName);

ENGINE* GetDigestEngineByAlgorithmName(const char* algorithmName);

ENGINE* GetAesEngineByAlgorithmName(const char* algorithmName);

ENGINE* GetSm4EngineByAlgorithmName(const char* algorithmName);

#endif
