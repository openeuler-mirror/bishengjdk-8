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
#include <string.h>
#include "kae_util.h"
#include "kae_exception.h"

static ENGINE* kaeEngine = NULL;

void SetKaeEngine(ENGINE* engine) {
    kaeEngine = engine;
}

ENGINE* GetKaeEngine() {
    return kaeEngine;
}

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
        KAE_ThrowNullPointerException(env, "GetByteArrayElements failed");
        goto cleanup;
    }
    BIGNUM* result = BN_bin2bn((const unsigned char*) bytes, len, bn);
    (*env)->ReleaseByteArrayElements(env, byteArray, bytes, 0);
    if (result == NULL) {
        KAE_ThrowFromOpenssl(env, "BN_bin2bn", KAE_ThrowRuntimeException);
        goto cleanup;
    }
    return bn;

cleanup:
    BN_free(bn);
    return NULL;
}

void KAE_ReleaseBigNumFromByteArray(BIGNUM* bn) {
    if (bn != NULL) {
        BN_free(bn);
    }
}

jbyteArray KAE_GetByteArrayFromBigNum(JNIEnv* env, const BIGNUM* bn) {
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
        KAE_ThrowOOMException(env, "New byte array failed.");
        return NULL;
    }
    jbyte* bytes = (*env)->GetByteArrayElements(env, javaBytes, NULL);
    if (bytes == NULL) {
        KAE_ThrowNullPointerException(env, "GetByteArrayElements failed.");
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

#define ENGINE_LENGTH (EC_INDEX + 1)
static ENGINE* engines[ENGINE_LENGTH] = {NULL};
static jboolean engineFlags[ENGINE_LENGTH] = {JNI_FALSE};
static KAEAlgorithm kaeAlgorithms[ENGINE_LENGTH] = {
        {MD5_INDEX,         "md5"},
        {SHA256_INDEX,      "sha256"},
        {SHA384_INDEX,      "sha384"},
        {SM3_INDEX,         "sm3"},
        {AES_128_ECB_INDEX, "aes-128-ecb"},
        {AES_128_CBC_INDEX, "aes-128-cbc"},
        {AES_128_CTR_INDEX, "aes-128-ctr"},
        {AES_128_GCM_INDEX, "aes-128-gcm"},
        {AES_192_ECB_INDEX, "aes-192-ecb"},
        {AES_192_CBC_INDEX, "aes-192-cbc"},
        {AES_192_CTR_INDEX, "aes-192-ctr"},
        {AES_192_GCM_INDEX, "aes-192-gcm"},
        {AES_256_ECB_INDEX, "aes-256-ecb"},
        {AES_256_CBC_INDEX, "aes-256-cbc"},
        {AES_256_CTR_INDEX, "aes-256-ctr"},
        {AES_256_GCM_INDEX, "aes-256-gcm"},
        {SM4_ECB_INDEX,     "sm4-ecb"},
        {SM4_CBC_INDEX,     "sm4-cbc"},
        {SM4_CTR_INDEX,     "sm4-ctr"},
        {SM4_OFB_INDEX,     "sm4-ofb"},
        {HMAC_MD5_INDEX,    "hmac-md5"},
        {HMAC_SHA1_INDEX,   "hmac-sha1"},
        {HMAC_SHA224_INDEX, "hmac-sha224"},
        {HMAC_SHA256_INDEX, "hmac-sha256"},
        {HMAC_SHA384_INDEX, "hmac-sha384"},
        {HMAC_SHA512_INDEX, "hmac-sha512"},
        {RSA_INDEX,         "rsa"},
        {DH_INDEX,          "dh"},
        {EC_INDEX,          "ec"}
};

void initEngines(JNIEnv* env, jbooleanArray algorithmKaeFlags) {
    if (algorithmKaeFlags == NULL) {
        return;
    }

    // get jTemp
    jboolean* jTemp = NULL;
    int length = (*env)->GetArrayLength(env, algorithmKaeFlags);
    jTemp = (jboolean*) malloc(length);
    if (jTemp == NULL) {
        KAE_ThrowOOMException(env, "initEngines GetArrayLength error");
        return;
    }
    (*env)->GetBooleanArrayRegion(env, algorithmKaeFlags, 0, length, jTemp);

    // assign engines
    int minLen = length < ENGINE_LENGTH ? length : ENGINE_LENGTH;
    int i;
    for (i = 0; i < minLen; i++) {
        if (jTemp[i]) {
            engines[i] = kaeEngine;
            engineFlags[i] = JNI_TRUE;
        }
    }
    if (length < ENGINE_LENGTH) {
        for (i = minLen; i < ENGINE_LENGTH; i++) {
            engines[i] = kaeEngine;
            engineFlags[i] = JNI_TRUE;
        }
    }

    // free jTemp
    if (jTemp != NULL) {
        free(jTemp);
    }
}

jbooleanArray getEngineFlags(JNIEnv* env) {
    jbooleanArray array = (*env)->NewBooleanArray(env, ENGINE_LENGTH);
    (*env)->SetBooleanArrayRegion(env, array, 0, ENGINE_LENGTH, engineFlags);
    return array;
}

ENGINE* GetEngineByAlgorithmIndex(AlgorithmIndex algorithmIndex) {
    return engines[algorithmIndex];
}

/*
 * Get the engine used by the specified algorithm.
 * @param beginIndex the beginning index, inclusive.
 * @param endIndex the ending index, exclusive.
 * @param algorithmName algorithm name
 * @return engine
 */
ENGINE* GetEngineByBeginIndexAndEndIndex(int beginIndex, int endIndex,
        const char* algorithmName) {
    if (beginIndex < 0 || endIndex > ENGINE_LENGTH) {
        return NULL;
    }

    int i;
    for (i = beginIndex; i < endIndex; i++) {
        if (strcasecmp(kaeAlgorithms[i].algorithmName, algorithmName) == 0) {
            return engines[kaeAlgorithms[i].algorithmIndex];
        }
    }
    return NULL;
}

ENGINE* GetHmacEngineByAlgorithmName(const char* algorithmName) {
    char prefix[] = {"hmac-"};
    int len = strlen(algorithmName);
    int newLen = strlen(algorithmName) + strlen(prefix) + 1;
    char* newAlgorithmName = NULL;
    newAlgorithmName = malloc(newLen);
    if (newAlgorithmName == NULL) {
        return NULL;
    }
    strcpy(newAlgorithmName, prefix);
    strcat(newAlgorithmName, algorithmName);
    ENGINE* engine = GetEngineByBeginIndexAndEndIndex(HMAC_MD5_INDEX, HMAC_SHA512_INDEX + 1, newAlgorithmName);
    if (newAlgorithmName != NULL) {
        free(newAlgorithmName);
    }
    return engine;
}

ENGINE* GetDigestEngineByAlgorithmName(const char* algorithmName) {
    return GetEngineByBeginIndexAndEndIndex(MD5_INDEX, SM3_INDEX + 1, algorithmName);
}

ENGINE* GetAesEngineByAlgorithmName(const char* algorithmName) {
    return GetEngineByBeginIndexAndEndIndex(AES_128_ECB_INDEX, AES_256_GCM_INDEX + 1, algorithmName);
}

ENGINE* GetSm4EngineByAlgorithmName(const char* algorithmName) {
    return GetEngineByBeginIndexAndEndIndex(SM4_ECB_INDEX, SM4_OFB_INDEX + 1, algorithmName);
}

