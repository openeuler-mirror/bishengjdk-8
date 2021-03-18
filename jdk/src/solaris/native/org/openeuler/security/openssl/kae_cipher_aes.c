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
#include <openssl/err.h>
#include <string.h>
#include "kae_log.h"
#include "kae_exception.h"
#include "org_openeuler_security_openssl_KAEAESCipher.h"

static const EVP_CIPHER* EVPGetCipherByName(JNIEnv* env, const char* algo)
{
    static const EVP_CIPHER* aes128Ecb = NULL;
    static const EVP_CIPHER* aes128Cbc = NULL;
    static const EVP_CIPHER* aes128Ctr = NULL;
    static const EVP_CIPHER* aes192Ecb = NULL;
    static const EVP_CIPHER* aes192Cbc = NULL;
    static const EVP_CIPHER* aes192Ctr = NULL;
    static const EVP_CIPHER* aes256Ecb = NULL;
    static const EVP_CIPHER* aes256Cbc = NULL;
    static const EVP_CIPHER* aes256Ctr = NULL;

    if (strcasecmp(algo, "aes-128-ecb") == 0) {
        return aes128Ecb == NULL ? aes128Ecb = EVP_get_cipherbyname(algo) : aes128Ecb;
    } else if (strcasecmp(algo, "aes-128-cbc") == 0) {
        return aes128Cbc == NULL ? aes128Cbc = EVP_get_cipherbyname(algo) : aes128Cbc;
    } else if (strcasecmp(algo, "aes-128-ctr") == 0) {
        return aes128Ctr == NULL ? aes128Ctr = EVP_get_cipherbyname(algo) : aes128Ctr;
    } else if (strcasecmp(algo, "aes-192-ecb") == 0) {
        return aes192Ecb == NULL ? aes192Ecb = EVP_get_cipherbyname(algo) : aes192Ecb;
    } else if (strcasecmp(algo, "aes-192-cbc") == 0) {
        return aes192Cbc == NULL ? aes192Cbc = EVP_get_cipherbyname(algo) : aes192Cbc;
    } else if (strcasecmp(algo, "aes-192-ctr") == 0) {
        return aes192Ctr == NULL ? aes192Ctr = EVP_get_cipherbyname(algo) : aes192Ctr;
    } else if (strcasecmp(algo, "aes-256-ecb") == 0) {
        return aes256Ecb == NULL ? aes256Ecb = EVP_get_cipherbyname(algo) : aes256Ecb;
    } else if (strcasecmp(algo, "aes-256-cbc") == 0) {
        return aes256Cbc == NULL ? aes256Cbc = EVP_get_cipherbyname(algo) : aes256Cbc;
    } else if (strcasecmp(algo, "aes-256-ctr") == 0) {
        return aes256Ctr == NULL ? aes256Ctr = EVP_get_cipherbyname(algo) : aes256Ctr;
    } else {
        KAE_ThrowRuntimeException(env, "EVPGetCipherByName error");
        return 0;
    }
}

/*
 * Class:     org_openeuler_security_openssl_KAEAESCipher
 * Method:    nativeInit
 * Signature: (Ljava/lang/String;Z[B[B)J
 */
JNIEXPORT jlong JNICALL
Java_org_openeuler_security_openssl_KAEAESCipher_nativeInit(JNIEnv* env, jclass cls,
    jstring cipherType, jboolean encrypt, jbyteArray key, jbyteArray iv, jboolean padding)
{
    EVP_CIPHER_CTX* ctx = NULL;
    jbyte* keyBytes = NULL;
    jbyte* ivBytes = NULL;
    const EVP_CIPHER* cipher = NULL;

    const char* algo = (*env)->GetStringUTFChars(env, cipherType, 0);
    cipher = EVPGetCipherByName(env, algo);
    (*env)->ReleaseStringUTFChars(env, cipherType, algo);
    if (cipher == NULL) {
        KAE_ThrowOOMException(env, "create EVP_CIPHER fail");
        goto err;
    }

    ctx = EVP_CIPHER_CTX_new();
    if (ctx == NULL) {
        KAE_ThrowOOMException(env, "create EVP_CIPHER_CTX fail");
        goto err;
    }

    if (iv != NULL) {
        ivBytes = (*env)->GetByteArrayElements(env, iv, NULL);
    }
    const unsigned char* i = (const unsigned char*) ivBytes;

    if (key != NULL) {
        keyBytes = (*env)->GetByteArrayElements(env, key, NULL);
    }
    const unsigned char* k = (const unsigned char*) keyBytes;

    if (!EVP_CipherInit_ex(ctx, cipher, NULL, k, i, encrypt ? 1 : 0)) {
        KAE_ThrowFromOpenssl(env, "EVP_CipherInit_ex failed", KAE_ThrowRuntimeException);
        goto err;
    }
    KAE_TRACE("KAEAESCipher_nativeInit EVP_CipherInit_ex(ctx = %p, cipher = %p, key = %p, iv = %p, encrypt = %d) "
              "success", ctx, cipher, key, iv, encrypt ? 1 : 0);

    EVP_CIPHER_CTX_set_padding(ctx, padding ? 1 : 0);

    if (iv != NULL) {
        (*env)->ReleaseByteArrayElements(env, iv, ivBytes, 0);
    }
    (*env)->ReleaseByteArrayElements(env, key, keyBytes, 0);
    return (jlong) ctx;
err:
    if (ctx != NULL) {
        EVP_CIPHER_CTX_free(ctx);
    }
    if (ivBytes != NULL) {
        (*env)->ReleaseByteArrayElements(env, iv, ivBytes, 0);
    }
    if (keyBytes != NULL) {
        (*env)->ReleaseByteArrayElements(env, key, keyBytes, 0);
    }
    return 0;
}

/*
 * Class:     org_openeuler_security_openssl_KAEAESCipher
 * Method:    nativeUpdate
 * Signature: (JZ[BII[BI)I
 */
JNIEXPORT jint JNICALL
Java_org_openeuler_security_openssl_KAEAESCipher_nativeUpdate(JNIEnv* env, jclass cls,
    jlong ctxAddress, jbyteArray inArr, jint inOfs, jint inLen, jbyteArray outArr, jint outOfs)
{
    jbyte* in = NULL;
    unsigned char* out = NULL;

    EVP_CIPHER_CTX* ctx = (EVP_CIPHER_CTX*) ctxAddress;
    if (ctx == NULL) {
        goto err;
    }

    if (inArr == NULL || outArr == NULL) {
        goto err;
    }
    int inputLen = (*env)->GetArrayLength(env, inArr);
    if ((inOfs < 0) || (inOfs > inputLen) || (inLen < 0) || (inLen > inputLen - inOfs)) {
        KAE_ThrowArrayIndexOutOfBoundsException(env, "inArr");
        goto err;
    }
    in = malloc(sizeof(jbyte) * inputLen);
    if (in == NULL) {
        KAE_ThrowOOMException(env, "malloc error");
        goto err;
    }
    (*env)->GetByteArrayRegion(env, inArr, 0, inputLen, in);

    int outputLen = (*env)->GetArrayLength(env, outArr);
    if ((outOfs < 0) || (outOfs > outputLen) || (inLen < 0) || (inLen > outputLen - outOfs)) {
        KAE_ThrowArrayIndexOutOfBoundsException(env, "outArr");
        goto err;
    }
    out = malloc(outputLen - outOfs);
    if (out == NULL) {
        KAE_ThrowOOMException(env, "malloc error");
        goto err;
    }

    unsigned int bytesWritten = 0;
    if (EVP_CipherUpdate(ctx, out, &bytesWritten, in + inOfs, inLen) == 0) {
        KAE_ThrowFromOpenssl(env, "EVP_CipherUpdate failed", KAE_ThrowRuntimeException);
        goto err;
    }
    KAE_TRACE("KAEAESCipher_nativeUpdate EVP_CipherUpdate success, bytesWritten = %d", bytesWritten);
    (*env)->SetByteArrayRegion(env, outArr, outOfs, bytesWritten, (jbyte*) out);

    free(in);
    free(out);
    return bytesWritten;
err:
    if (in != NULL) {
        free(in);
    }
    if (out != NULL) {
        free(out);
    }
    return 0;
}

/*
 * Class:     org_openeuler_security_openssl_KAEAESCipher
 * Method:    nativeFinal
 * Signature: (JZ[BI)I
 */
JNIEXPORT jint JNICALL
Java_org_openeuler_security_openssl_KAEAESCipher_nativeFinal(JNIEnv* env, jclass cls,
    jlong ctxAddress, jbyteArray outArr, jint outOfs)
{
    unsigned char* out;
    EVP_CIPHER_CTX* ctx = (EVP_CIPHER_CTX*) ctxAddress;
    KAE_TRACE("KAEAESCipher_nativeFinal(ctxAddress = %p, outArr = %p, outOfs = %d)",
              ctx, outArr, outOfs);
    if (ctx == NULL) {
        goto err;
    }
    if (outArr == NULL) {
        goto err;
    }
    int outputLen = (*env)->GetArrayLength(env, outArr);
    out = malloc(outputLen - outOfs);
    if (out == NULL) {
        KAE_ThrowOOMException(env, "malloc error");
        goto err;
    }
    unsigned int bytesWritten = 0;
    int result_code = EVP_CipherFinal_ex(ctx, out, &bytesWritten);
    if (result_code == 0) {
        KAE_ThrowFromOpenssl(env, "EVP_CipherFinal_ex failed", KAE_ThrowBadPaddingException);
        goto err;
    }
    KAE_TRACE("KAEAESCipher_nativeFinal EVP_CipherFinal_ex success, bytesWritten = %d", bytesWritten);
    (*env)->SetByteArrayRegion(env, outArr, outOfs, bytesWritten, (jbyte*) out);
    free(out);
    KAE_TRACE("KAEAESCipher_nativeFinal: finished");
    return bytesWritten;

err:
    if (out != NULL) {
        free(out);
    }
    return 0;
}

/*
 * Class:     org_openeuler_security_openssl_KAEAESCipher
 * Method:    nativeFree
 * Signature: (J)V
 */
JNIEXPORT void JNICALL
Java_org_openeuler_security_openssl_KAEAESCipher_nativeFree(JNIEnv* env, jclass cls, jlong ctxAddress)
{
    EVP_CIPHER_CTX* ctx = (EVP_CIPHER_CTX*) ctxAddress;
    KAE_TRACE("KAEAESCipher_nativeFree(ctx = %p)", ctx);
    if (ctx != NULL) {
        EVP_CIPHER_CTX_free(ctx);
    }

    KAE_TRACE("KAEAESCipher_nativeFree: finished");
}
