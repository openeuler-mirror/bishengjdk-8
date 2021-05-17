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

#include <jni.h>
#include <string.h>
#include <openssl/hmac.h>
#include "kae_exception.h"
#include "kae_log.h"
#include "kae_util.h"

static const EVP_MD* EVPGetDigestByName(JNIEnv* env, const char* algo)
{
    static const EVP_MD* md5     = NULL;
    static const EVP_MD* sha1    = NULL;
    static const EVP_MD* sha224  = NULL;
    static const EVP_MD* sha256  = NULL;
    static const EVP_MD* sha384  = NULL;
    static const EVP_MD* sha512  = NULL;

    if (strcasecmp(algo, "md5") == 0) {
        return md5 == NULL ? md5 = EVP_get_digestbyname(algo) : md5;
    } else if (strcasecmp(algo, "sha1") == 0) {
        return sha1 == NULL ? sha1 = EVP_get_digestbyname(algo) : sha1;
    } else if (strcasecmp(algo, "sha224") == 0) {
        return sha224 == NULL ? sha224 = EVP_get_digestbyname(algo) : sha224;
    } else if (strcasecmp(algo, "sha256") == 0) {
        return sha256 == NULL ? sha256 = EVP_get_digestbyname(algo) : sha256;
    } else if (strcasecmp(algo, "sha384") == 0) {
        return sha384 == NULL ? sha384 = EVP_get_digestbyname(algo) : sha384;
    } else if (strcasecmp(algo, "sha512") == 0) {
        return sha512 == NULL ? sha512 = EVP_get_digestbyname(algo) : sha512;
    } else {
        KAE_ThrowRuntimeException(env, "EVPGetDigestByName error");
        return 0;
    }
}

/*
 * Class:     org_openeuler_security_openssl_KAEHMac
 * Method:    nativeInit
 * Signature: ([BILjava/lang/String;)J
 */
JNIEXPORT jlong JNICALL Java_org_openeuler_security_openssl_KAEHMac_nativeInit
    (JNIEnv* env, jclass cls, jbyteArray key, jint key_len, jstring algoStr) {
    if (key == NULL || algoStr == NULL) {
        KAE_ThrowNullPointerException(env, "param key or algoStr is null");
        return 0;
    }
    if (key_len <= 0) {
        KAE_ThrowArrayIndexOutOfBoundsException(env, "key");
        return 0;
    }
    HMAC_CTX* ctx = NULL;
    jbyte* key_buffer = NULL;
    const EVP_MD* md = NULL;

    const char* algo = (*env)->GetStringUTFChars(env, algoStr, 0);
    md  = EVPGetDigestByName(env, algo);
    (*env)->ReleaseStringUTFChars(env, algoStr, algo);
    if (md == NULL) {
        KAE_ThrowRuntimeException(env, "algorithm unsupport");
        return 0;
    }

    // get secret-key
    key_buffer = malloc(key_len);
    if (key_buffer == NULL) {
        KAE_ThrowOOMException(env, "malloc failed");
        return 0;
    }
    (*env)->GetByteArrayRegion(env, key, 0, key_len, key_buffer);

    // create a hmac context
    ctx = HMAC_CTX_new();
    if (ctx == NULL) {
        KAE_ThrowRuntimeException(env, "Hmac_CTX_new invoked failed");
        goto cleanup;
    }

    // init hmac context with sc_key and evp_md
    int result_code = HMAC_Init_ex(ctx, key_buffer, key_len, md, NULL);
    if (result_code == 0) {
        KAE_ThrowRuntimeException(env, "Hmac_Init_ex invoked failed");
        goto cleanup;
    }
    free(key_buffer);
    return (jlong) ctx;

cleanup:
    free(key_buffer);
    HMAC_CTX_free(ctx);
    return 0;
}

/*
 * Class:     org_openeuler_security_openssl_KAEHMac
 * Method:    nativeUpdate
 * Signature: (J[BII)V
 */
JNIEXPORT void JNICALL Java_org_openeuler_security_openssl_KAEHMac_nativeUpdate
    (JNIEnv* env, jclass cls, jlong hmac_ctx, jbyteArray input, jint in_offset, jint in_len) {
    KAE_TRACE("KAEHMac_nativeUpdate(ctx = %p, input = %p, offset = %d, inLen = %d", hmac_ctx, input, in_offset, in_len);
    HMAC_CTX* ctx = (HMAC_CTX*) hmac_ctx;
    if (ctx == NULL || input == NULL) {
        KAE_ThrowNullPointerException(env, "param ctx or input is null");
        return;
    }
    int input_size = (*env)->GetArrayLength(env, input);
    if ((in_offset < 0) || (in_len < 0) || (in_offset > input_size - in_len)) {
        KAE_ThrowArrayIndexOutOfBoundsException(env, "input");
        return;
    }
    // do nothing while in_len is 0
    if (in_len == 0) {
        return;
    }

    jbyte* buffer = malloc(in_len);
    if (buffer == NULL) {
        KAE_ThrowOOMException(env, "malloc failed");
        return;
    }
    (*env)->GetByteArrayRegion(env, input, in_offset, in_len, buffer);
    if (!HMAC_Update(ctx, (unsigned char*) buffer, in_len)) {
        KAE_ThrowRuntimeException(env, "Hmac_Update invoked failed");
    }
    free(buffer);
}

/*
 * Class:     org_openeuler_security_openssl_KAEHMac
 * Method:    nativeFinal
 * Signature: (J[BII)I
 */
JNIEXPORT jint JNICALL Java_org_openeuler_security_openssl_KAEHMac_nativeFinal
    (JNIEnv* env, jclass cls, jlong hmac_ctx, jbyteArray output, jint out_offset, jint in_len) {
    HMAC_CTX* ctx = (HMAC_CTX*) hmac_ctx;
    if (ctx == NULL || output == NULL) {
        KAE_ThrowNullPointerException(env, "param ctx or input is null");
        return 0;
    }
    int output_size = (*env)->GetArrayLength(env, output);
    if ((out_offset < 0) || (in_len < 0) || (out_offset > output_size - in_len)) {
        KAE_ThrowArrayIndexOutOfBoundsException(env, "output");
        return 0;
    }

    jbyte* temp_result = NULL;

    temp_result = malloc(in_len);
    if (temp_result == NULL) {
        KAE_ThrowOOMException(env, "malloc failed");
        return 0;
    }
    // do final
    unsigned int bytesWritten = 0;
    int result_code = HMAC_Final(ctx, (unsigned char*) temp_result, &bytesWritten);
    if (result_code == 0) {
        KAE_ThrowRuntimeException(env, "Hmac_Final invoked failed");
        goto cleanup;
    }

    // write back to output_array
    (*env)->SetByteArrayRegion(env, output, out_offset, bytesWritten, (jbyte*) temp_result);
    KAE_TRACE("KAEHMac_nativeFinal success, output_offset = %d, bytesWritten = %d", out_offset, bytesWritten);

cleanup:
    free(temp_result);
    return bytesWritten;
}

/*
 * Class:     org_openeuler_security_openssl_KAEHMac
 * Method:    nativeFree
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_org_openeuler_security_openssl_KAEHMac_nativeFree
    (JNIEnv* env, jclass cls, jlong hmac_ctx) {
    HMAC_CTX* ctx = (HMAC_CTX*) hmac_ctx;
    if (ctx != NULL) {
        HMAC_CTX_free(ctx);
    }
}
