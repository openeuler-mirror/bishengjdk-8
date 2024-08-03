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
#include <stdbool.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <string.h>
#include "kae_exception.h"
#include "kae_log.h"
#include "kae_util.h"
#include "org_openeuler_security_openssl_KAESymmetricCipherBase.h"

bool StartsWith(const char* str1, const char* str2)
{
    if (str1 == NULL || str2 == NULL) {
        return 0;
    }
    int len1 = strlen(str1);
    int len2 = strlen(str2);
    if (len1 > len2 || (len1 == 0 || len2 == 0)) {
        return false;
    }
    const char *cur = str1;
    int i = 0;
    while (*cur != '\0') {
        if (*cur != str2[i]) {
            return 0;
        }
        cur++;
        i++;
    }
    return true;
}

static const EVP_CIPHER* EVPGetSm4CipherByName(JNIEnv* env, const char* algo)
{
    static const EVP_CIPHER* sm4Ecb = NULL;
    static const EVP_CIPHER* sm4Cbc = NULL;
    static const EVP_CIPHER* sm4Ctr = NULL;
    static const EVP_CIPHER* sm4Ofb = NULL;

    if (strcasecmp(algo, "sm4-ecb") == 0) {
        return sm4Ecb == NULL ? sm4Ecb = EVP_get_cipherbyname(algo) : sm4Ecb;
    } else if (strcasecmp(algo, "sm4-cbc") == 0) {
        return sm4Cbc == NULL ? sm4Cbc = EVP_get_cipherbyname(algo) : sm4Cbc;
    } else if (strcasecmp(algo, "sm4-ctr") == 0) {
        return sm4Ctr == NULL ? sm4Ctr = EVP_get_cipherbyname(algo) : sm4Ctr;
    } else if (strcasecmp(algo, "sm4-ofb") == 0) {
        return sm4Ofb == NULL ? sm4Ofb = EVP_get_cipherbyname(algo) : sm4Ofb;
    } else {
        KAE_ThrowRuntimeException(env, "EVPGetSm4CipherByName error");
        return 0;
    }
}

static const EVP_CIPHER* EVPGetAesCipherByName(JNIEnv* env, const char* algo)
{
    static const EVP_CIPHER* aes128Ecb = NULL;
    static const EVP_CIPHER* aes128Cbc = NULL;
    static const EVP_CIPHER* aes128Ctr = NULL;
    static const EVP_CIPHER* aes128Gcm = NULL;
    static const EVP_CIPHER* aes192Ecb = NULL;
    static const EVP_CIPHER* aes192Cbc = NULL;
    static const EVP_CIPHER* aes192Ctr = NULL;
    static const EVP_CIPHER* aes192Gcm = NULL;
    static const EVP_CIPHER* aes256Ecb = NULL;
    static const EVP_CIPHER* aes256Cbc = NULL;
    static const EVP_CIPHER* aes256Ctr = NULL;
    static const EVP_CIPHER* aes256Gcm = NULL;

    if (strcasecmp(algo, "aes-128-ecb") == 0) {
        return aes128Ecb == NULL ? aes128Ecb = EVP_get_cipherbyname(algo) : aes128Ecb;
    } else if (strcasecmp(algo, "aes-128-cbc") == 0) {
        return aes128Cbc == NULL ? aes128Cbc = EVP_get_cipherbyname(algo) : aes128Cbc;
    } else if (strcasecmp(algo, "aes-128-ctr") == 0) {
        return aes128Ctr == NULL ? aes128Ctr = EVP_get_cipherbyname(algo) : aes128Ctr;
    } else if (strcasecmp(algo, "aes-128-gcm") == 0) {
        return aes128Gcm == NULL ? aes128Gcm = EVP_get_cipherbyname(algo) : aes128Gcm;
    } else if (strcasecmp(algo, "aes-192-ecb") == 0) {
        return aes192Ecb == NULL ? aes192Ecb = EVP_get_cipherbyname(algo) : aes192Ecb;
    } else if (strcasecmp(algo, "aes-192-cbc") == 0) {
        return aes192Cbc == NULL ? aes192Cbc = EVP_get_cipherbyname(algo) : aes192Cbc;
    } else if (strcasecmp(algo, "aes-192-ctr") == 0) {
        return aes192Ctr == NULL ? aes192Ctr = EVP_get_cipherbyname(algo) : aes192Ctr;
    } else if (strcasecmp(algo, "aes-192-gcm") == 0) {
        return aes192Gcm == NULL ? aes192Gcm = EVP_get_cipherbyname(algo) : aes192Gcm;
    } else if (strcasecmp(algo, "aes-256-ecb") == 0) {
        return aes256Ecb == NULL ? aes256Ecb = EVP_get_cipherbyname(algo) : aes256Ecb;
    } else if (strcasecmp(algo, "aes-256-cbc") == 0) {
        return aes256Cbc == NULL ? aes256Cbc = EVP_get_cipherbyname(algo) : aes256Cbc;
    } else if (strcasecmp(algo, "aes-256-ctr") == 0) {
        return aes256Ctr == NULL ? aes256Ctr = EVP_get_cipherbyname(algo) : aes256Ctr;
    } else if (strcasecmp(algo, "aes-256-gcm") == 0) {
        return aes256Gcm == NULL ? aes256Gcm = EVP_get_cipherbyname(algo) : aes256Gcm;
    } else {
        KAE_ThrowRuntimeException(env, "EVPGetAesCipherByName error");
        return 0;
    }
}

void FreeMemoryFromInit(JNIEnv* env, jbyteArray iv, jbyte* ivBytes, jbyteArray key, jbyte* keyBytes,
    int keyLength)
{
    if (ivBytes != NULL) {
        (*env)->ReleaseByteArrayElements(env, iv, ivBytes, 0);
    }
    if (keyBytes != NULL) {
        memset(keyBytes, 0, keyLength);
        (*env)->ReleaseByteArrayElements(env, key, keyBytes, JNI_ABORT);
    }
}

/*
 * Class:     org_openeuler_security_openssl_KAESymmetricCipherBase
 * Method:    nativeInit
 * Signature: (Ljava/lang/String;Z[B[B)J
 */
JNIEXPORT jlong JNICALL
Java_org_openeuler_security_openssl_KAESymmetricCipherBase_nativeInit(JNIEnv* env, jclass cls,
    jstring cipherType, jboolean encrypt, jbyteArray key, jbyteArray iv, jboolean padding)
{
    EVP_CIPHER_CTX* ctx = NULL;
    jbyte* keyBytes = NULL;
    jbyte* ivBytes = NULL;
    const EVP_CIPHER* cipher = NULL;
    ENGINE* kaeEngine = NULL;
    int keyLength = (*env)->GetArrayLength(env, key);
    int ivLength = 0;

    const char* algo = (*env)->GetStringUTFChars(env, cipherType, 0);
    if (StartsWith("aes", algo)) {
        cipher = EVPGetAesCipherByName(env, algo);
        kaeEngine = GetAesEngineByAlgorithmName(algo);
    } else {
        cipher = EVPGetSm4CipherByName(env, algo);
        kaeEngine = GetSm4EngineByAlgorithmName(algo);
    }

    KAE_TRACE("KAESymmetricCipherBase_nativeInit: kaeEngine => %p", kaeEngine);

    if (cipher == NULL) {
        KAE_ThrowOOMException(env, "create EVP_CIPHER fail");
        goto cleanup;
    }
    if ((ctx = EVP_CIPHER_CTX_new()) == NULL) {
        KAE_ThrowOOMException(env, "create EVP_CIPHER_CTX fail");
        goto cleanup;
    }

    if (iv != NULL) {
        ivBytes = (*env)->GetByteArrayElements(env, iv, NULL);
        ivLength = (*env)->GetArrayLength(env, iv);
    }
    if (key != NULL) {
        keyBytes = (*env)->GetByteArrayElements(env, key, NULL);
    }

    if (!EVP_CipherInit_ex(ctx, cipher, kaeEngine, NULL,
            NULL, encrypt ? 1 : 0)) {
        KAE_ThrowFromOpenssl(env, "EVP_CipherInit_ex failed", KAE_ThrowRuntimeException);
        goto cleanup;
    }

    if (strcasecmp(algo + 8, "gcm") == 0) {
        /* Set IV length if default 12 bytes (96 bits) is not appropriate */
        if(!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, ivLength, NULL)) {
            KAE_ThrowFromOpenssl(env, "EVP_CIPHER_CTX_ctrl failed", KAE_ThrowRuntimeException);
            goto cleanup;
        }
    }

    if (!EVP_CipherInit_ex(ctx, NULL, kaeEngine, (const unsigned char*)keyBytes,
            (const unsigned char*)ivBytes, encrypt ? 1 : 0)) {
        KAE_ThrowFromOpenssl(env, "EVP_CipherInit_ex int key & iv failed", KAE_ThrowRuntimeException);
        goto cleanup;
    }

    EVP_CIPHER_CTX_set_padding(ctx, padding ? 1 : 0);

    (*env)->ReleaseStringUTFChars(env, cipherType, algo);
    FreeMemoryFromInit(env, iv, ivBytes, key, keyBytes, keyLength);
    return (jlong)ctx;

cleanup:
    if (ctx != NULL) {
        EVP_CIPHER_CTX_free(ctx);
    }
    (*env)->ReleaseStringUTFChars(env, cipherType, algo);
    FreeMemoryFromInit(env, iv, ivBytes, key, keyBytes, keyLength);
    return 0;
}

static void FreeMemoryFromUpdate(unsigned char* in, unsigned char* aad, unsigned char* out)
{
    if (in != NULL) {
        free(in);
    }
    if (out != NULL) {
        free(out);
    }
    if (aad != NULL) {
        free(aad);
    }
}

/*
 * Class:     org_openeuler_security_openssl_KAESymmetricCipherBase
 * Method:    nativeUpdate
 * Signature: (J[BII[BIZ[B)I
 */
JNIEXPORT jint JNICALL
Java_org_openeuler_security_openssl_KAESymmetricCipherBase_nativeUpdate(JNIEnv* env, jclass cls, jlong ctxAddress,
    jbyteArray inArr, jint inOfs, jint inLen, jbyteArray outArr, jint outOfs, jboolean gcm, jbyteArray gcmAAD)
{
    unsigned char* in = NULL;
    unsigned char* aad = NULL;
    unsigned char* out = NULL;

    EVP_CIPHER_CTX* ctx = (EVP_CIPHER_CTX*)ctxAddress;
    if (ctx == NULL || inArr == NULL || outArr == NULL) {
        goto cleanup;
    }

    in = (unsigned char*)malloc(inLen);
    int outLen = (*env)->GetArrayLength(env, outArr) - outOfs;
    out = (unsigned char*)malloc(outLen);
    if (in == NULL || out == NULL) {
        KAE_ThrowOOMException(env, "malloc error");
        goto cleanup;
    }
    memset(in, 0, inLen);
    memset(out, 0, outLen);
    (*env)->GetByteArrayRegion(env, inArr, inOfs, inLen, (jbyte*)in);

    int bytesWritten = 0;
    if (gcm && (gcmAAD != NULL)) {
        int aadLen = (*env)->GetArrayLength(env, gcmAAD);
        if ((aad = (unsigned char*)malloc(aadLen)) == NULL) {
            KAE_ThrowOOMException(env, "malloc error");
            goto cleanup;
        }
        memset(aad, 0, aadLen);
        (*env)->GetByteArrayRegion(env, gcmAAD, 0, aadLen, (jbyte*)aad);

        // Specify aad.
        if (EVP_CipherUpdate(ctx, NULL, &bytesWritten, aad, aadLen) == 0) {
            KAE_ThrowFromOpenssl(env, "EVP_CipherUpdate failed", KAE_ThrowRuntimeException);
            goto cleanup;
        }
    }

    if (EVP_CipherUpdate(ctx, out, &bytesWritten, in, inLen) == 0) {
        KAE_ThrowFromOpenssl(env, "EVP_CipherUpdate failed", KAE_ThrowRuntimeException);
        goto cleanup;
    }
    (*env)->SetByteArrayRegion(env, outArr, outOfs, bytesWritten, (jbyte*)out);

    FreeMemoryFromUpdate(in, aad, out);
    return bytesWritten;

cleanup:
    FreeMemoryFromUpdate(in, aad, out);
    return 0;
}

/*
 * Class:     org_openeuler_security_openssl_KAESymmetricCipherBase
 * Method:    nativeFinal
 * Signature: (JZ[BI)I
 */
JNIEXPORT jint JNICALL
Java_org_openeuler_security_openssl_KAESymmetricCipherBase_nativeFinal(JNIEnv* env, jclass cls,
    jlong ctxAddress, jbyteArray outArr, jint outOfs)
{
    unsigned char* out = NULL;
    EVP_CIPHER_CTX* ctx = (EVP_CIPHER_CTX*)ctxAddress;
    KAE_TRACE("KAESymmetricCipherBase_nativeFinal(ctxAddress = %p, outArr = %p, outOfs = %d)",
              ctx, outArr, outOfs);
    if (ctx == NULL || outArr == NULL) {
        goto cleanup;
    }
    int outLen = (*env)->GetArrayLength(env, outArr) - outOfs;
    out = (unsigned char*)malloc(outLen);
    if (out == NULL) {
        KAE_ThrowOOMException(env, "malloc error");
        goto cleanup;
    }
    memset(out, 0, outLen);
    int bytesWritten = 0;
    int result_code = EVP_CipherFinal_ex(ctx, out, &bytesWritten);
    if (result_code == 0) {
        KAE_ThrowFromOpenssl(env, "EVP_CipherFinal_ex failed", KAE_ThrowBadPaddingException);
        goto cleanup;
    }
    KAE_TRACE("KAESymmetricCipherBase_nativeFinal EVP_CipherFinal_ex success, bytesWritten = %d", bytesWritten);
    (*env)->SetByteArrayRegion(env, outArr, outOfs, bytesWritten, (jbyte*)out);
    free(out);
    KAE_TRACE("KAESymmetricCipherBase_nativeFinal: finished");
    return bytesWritten;

cleanup:
    if (out != NULL) {
        free(out);
    }
    return 0;
}

static void FreeMemoryFromFinalGcm(unsigned char* out, unsigned char* gcmTag, unsigned char* gcmOut)
{
    if (out != NULL) {
        free(out);
    }
    if (gcmTag != NULL) {
        free(gcmTag);
    }
    if (gcmOut != NULL) {
        free(gcmOut);
    }
}

/*
 * Class:     org_openeuler_security_openssl_KAECipherAES
 * Method:    nativeFinalGcm
 * Signature: (J[BIZI[BZ)I
 */
JNIEXPORT jint JNICALL Java_org_openeuler_security_openssl_KAESymmetricCipherBase_nativeFinalGcm(JNIEnv* env,
        jclass cls, jlong ctxAddress, jbyteArray outArr, jint outOfs, jboolean gcm, jint tagLength,
        jbyteArray gcmTagArr, jboolean encrypt)
{
    unsigned char* out = NULL;
    unsigned char* gcmTag = NULL;
    unsigned char* gcmOut = NULL;

    EVP_CIPHER_CTX* ctx = (EVP_CIPHER_CTX*)ctxAddress;
    if (ctx == NULL || outArr == NULL) {
        goto cleanup;
    }

    int bytesWritten = 0;
    if (encrypt) {
        int outLen = (*env)->GetArrayLength(env, outArr) - outOfs;
        if ((out = malloc(outLen)) == NULL) {
            KAE_ThrowOOMException(env, "malloc error");
            goto cleanup;
        }
        memset(out, 0, outLen);
        if (EVP_CipherFinal_ex(ctx, out, &bytesWritten) == 0) {
            KAE_ThrowFromOpenssl(env, "EVP_CipherFinal_ex failed", KAE_ThrowBadPaddingException);
            goto cleanup;
        }
        (*env)->SetByteArrayRegion(env, outArr, outOfs, bytesWritten, (jbyte*)out);

        // Writes tagLength bytes of the tag value to the buffer.
        // Refer to {@link https://www.openssl.org/docs/man1.1.0/man3/EVP_CIPHER_CTX_ctrl.html} for details.
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG, tagLength, out + bytesWritten) == 0) {
            KAE_ThrowFromOpenssl(env, "EVP_CIPHER_CTX_ctrl failed", KAE_ThrowRuntimeException);
            goto cleanup;
        }
        (*env)->SetByteArrayRegion(env, outArr, outOfs + bytesWritten, tagLength, (jbyte*)(out + bytesWritten));
        bytesWritten += tagLength;
    } else {
        // gcmOut is the plaintext that has been decrypted in the EVP_CipherUpdate.
        // outOfs is the length of the gcmOut, where it's always > 0.
        if ((gcmTag = (unsigned char*)malloc(tagLength)) == NULL || (gcmOut = (unsigned char*)malloc(outOfs)) == NULL) {
            KAE_ThrowOOMException(env, "malloc error");
            goto cleanup;
        }
        memset(gcmTag, 0, tagLength);
        memset(gcmOut, 0, outOfs);

        (*env)->GetByteArrayRegion(env, gcmTagArr, 0, tagLength, (jbyte*)gcmTag);
        // Sets the expected gcmTag to tagLength bytes from gcmTag.
        // Refer to {@link https://www.openssl.org/docs/man1.1.0/man3/EVP_CIPHER_CTX_ctrl.html} for details.
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG, tagLength, gcmTag) == 0) {
            KAE_ThrowFromOpenssl(env, "EVP_CTRL_AEAD_SET_TAG failed", KAE_ThrowRuntimeException);
            goto cleanup;
        }

        (*env)->GetByteArrayRegion(env, outArr, 0, outOfs, (jbyte*)gcmOut);
        // Finalise: note get no output for GCM
        if (EVP_CipherFinal_ex(ctx, gcmOut, &bytesWritten) == 0) {
            KAE_ThrowFromOpenssl(env, "EVP_CipherFinal_ex failed", KAE_ThrowAEADBadTagException);
            goto cleanup;
        }
    }
    FreeMemoryFromFinalGcm(out, gcmTag, gcmOut);

    return bytesWritten;

cleanup:
    FreeMemoryFromFinalGcm(out, gcmTag, gcmOut);
    return 0;
}

/*
 * Class:     org_openeuler_security_openssl_KAESymmetricCipherBase
 * Method:    nativeFree
 * Signature: (J)V
 */
JNIEXPORT void JNICALL
Java_org_openeuler_security_openssl_KAESymmetricCipherBase_nativeFree(JNIEnv* env, jclass cls, jlong ctxAddress)
{
    EVP_CIPHER_CTX* ctx = (EVP_CIPHER_CTX*)ctxAddress;
    KAE_TRACE("KAESymmetricCipherBase_nativeFree(ctx = %p)", ctx);
    if (ctx != NULL) {
        EVP_CIPHER_CTX_free(ctx);
        ctx = NULL;
    }

    KAE_TRACE("KAESymmetricCipherBase_nativeFree: finished");
}
