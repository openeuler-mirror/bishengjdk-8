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

#include <string.h>
#include <openssl/evp.h>
#include <openssl/md5.h>
#include "kae_exception.h"
#include "kae_log.h"
#include "kae_util.h"
#include "org_openeuler_security_openssl_KAEDigest.h"

#define DIGEST_STACK_SIZE 1024
#define DIGEST_CHUNK_SIZE 64*1024
#define DIGEST_LENGTH_THRESHOLD 48

/*
 * Class:     org_openeuler_security_openssl_KAEDigest
 * Method:    nativeInit
 * Signature: (Ljava/lang/String;)J
 */
JNIEXPORT jlong JNICALL
Java_org_openeuler_security_openssl_KAEDigest_nativeInit(JNIEnv *env, jclass cls, jstring algorithmName)
{
    EVP_MD_CTX* ctx = NULL;
    static ENGINE* kaeEngine = NULL;

    if (algorithmName == NULL) {
        KAE_ThrowNullPointerException(env, "algorithm is null");
        return 0;
    }

    // EVP_get_digestbyname
    const char* algo_utf = (*env)->GetStringUTFChars(env, algorithmName, 0);
    if ((strcasecmp(algo_utf, "md5") == 0) || (strcasecmp(algo_utf, "sm3") == 0)) {
        kaeEngine = (kaeEngine == NULL) ? GetKaeEngine() : kaeEngine;
    } else {
        kaeEngine = NULL;
    }
    EVP_MD* md = (EVP_MD*) EVP_get_digestbyname(algo_utf);
    (*env)->ReleaseStringUTFChars(env, algorithmName, algo_utf);
    if (md == NULL) {
        KAE_TRACE("%s not supported", algo_utf);
        return 0;
    }
    KAE_TRACE("KAEDigest_nativeInit: create md => %p", md);

    ctx = EVP_MD_CTX_create();
    if (ctx == NULL) {
        KAE_ThrowOOMException(env, "create EVP_MD_CTX fail");
        return 0;
    }
    KAE_TRACE("KAEDigest_nativeInit: create ctx => %p", ctx);

    // EVP_DigestInit_ex
    int result_code = EVP_DigestInit_ex(ctx, md, kaeEngine);
    if (result_code == 0) {
        KAE_ThrowFromOpenssl(env, "EVP_DigestInit_ex failed", KAE_ThrowRuntimeException);
        goto cleanup;
    }
    KAE_TRACE("KAEDigest_nativeInit EVP_DigestInit_ex(ctx = %p, md = %p) success", ctx, md);

    KAE_TRACE("KAEDigest_nativeInit: finished");
    return (jlong) ctx;

cleanup:
    EVP_MD_CTX_destroy(ctx);
    return 0;
}

/*
 * Class:     org_openeuler_security_openssl_KAEDigest
 * Method:    nativeUpdate
 * Signature: (Ljava/lang/String;J[BII)I
 */
JNIEXPORT void JNICALL
Java_org_openeuler_security_openssl_KAEDigest_nativeUpdate(JNIEnv *env, jclass cls, jlong ctxAddress,
    jbyteArray input, jint offset, jint inLen)
{
    EVP_MD_CTX* ctx = (EVP_MD_CTX*) ctxAddress;
    KAE_TRACE("KAEDigest_nativeUpdate(ctx = %p, input = %p, offset = %d, inLen = %d", ctx, input, offset, inLen);
    if (ctx == NULL) {
        return;
    }

    jint in_offset = offset;
    jint in_size = inLen;
    int result_code = 0;
    if (in_size <= DIGEST_STACK_SIZE) { // allocation on the stack
        jbyte buffer[DIGEST_STACK_SIZE];
        (*env)->GetByteArrayRegion(env, input, offset, inLen, buffer);
        result_code = EVP_DigestUpdate(ctx, buffer, inLen);
    } else { // data chunk
        jint remaining = in_size;
        jint buf_size = (remaining >= DIGEST_CHUNK_SIZE) ? DIGEST_CHUNK_SIZE : remaining;
        jbyte* buffer = malloc(buf_size);
        if (buffer == NULL) {
            KAE_ThrowOOMException(env, "malloc error");
            return;
        }
        while (remaining > 0) {
            jint chunk_size = (remaining >= buf_size) ? buf_size : remaining;
            (*env)->GetByteArrayRegion(env, input, in_offset, chunk_size, buffer);
            result_code = EVP_DigestUpdate(ctx, buffer, chunk_size);
            if (!result_code) {
                break;
            }
            in_offset += chunk_size;
            remaining -= chunk_size;
        }
        free(buffer);
    }
    if (!result_code) {
        KAE_ThrowFromOpenssl(env, "EVP_DigestUpdate failed", KAE_ThrowRuntimeException);
        return;
    }
    KAE_TRACE("KAEDigest_nativeUpdate EVP_DigestUpdate success");
    KAE_TRACE("KAEDigest_nativeUpdate: finished");
}

/*
 * Class:     org_openeuler_security_openssl_KAEDigest
 * Method:    nativeDigest
 * Signature: (Ljava/lang/String;J[BII)I
 */
JNIEXPORT jint JNICALL
Java_org_openeuler_security_openssl_KAEDigest_nativeDigest(JNIEnv *env, jclass cls,
    jlong ctxAddress, jbyteArray output, jint offset, jint len)
{
    EVP_MD_CTX* ctx = (EVP_MD_CTX*) ctxAddress;
    KAE_TRACE("KAEDigest_nativeDigest(ctx = %p, output = %p, offset = %d, len = %d", ctx, output, offset, len);
    unsigned char* md = NULL;
    unsigned int bytesWritten = 0;

    if (ctx == NULL) {
        return 0;
    }

    if (len <= 0 || len > DIGEST_LENGTH_THRESHOLD) {
        KAE_ThrowRuntimeException(env, "len out of length");
        return 0;
    }
    md = malloc(len);
    if (md == NULL) {
        KAE_ThrowOOMException(env, "malloc error");
        return 0;
    }

    // EVP_DigestFinal_ex
    int result_code = EVP_DigestFinal_ex(ctx, md, &bytesWritten);
    if (result_code == 0) {
        KAE_ThrowFromOpenssl(env, "EVP_DigestFinal_ex failed", KAE_ThrowRuntimeException);
        goto cleanup;
    }
    KAE_TRACE("KAEDigest_nativeFinal EVP_DigestFinal_ex success, bytesWritten = %d", bytesWritten);

    (*env)->SetByteArrayRegion(env, output, offset, bytesWritten, (jbyte*) md);

    KAE_TRACE("KAEDigest_nativeFinal: finished");

cleanup:
    free(md);
    return bytesWritten;
}

/*
* Class:     org_openeuler_security_openssl_KAEDigest
* Method:    nativeClone
* Signature: (J)J
*/
JNIEXPORT jlong JNICALL
Java_org_openeuler_security_openssl_KAEDigest_nativeClone(JNIEnv *env, jclass cls, jlong ctxAddress)
{
    EVP_MD_CTX* ctx = (EVP_MD_CTX*) ctxAddress;
    KAE_TRACE("KAEDigest_nativeClone: ctx = %p", ctx);
    if (ctx == NULL) {
        return 0;
    }

    EVP_MD_CTX* ctxCopy = EVP_MD_CTX_create();
    if (ctxCopy == NULL) {
        KAE_ThrowOOMException(env, "create EVP_MD_CTX fail");
        return 0;
    }
    KAE_TRACE("KAEDigest_nativeClone: create ctxCopy => %p", ctxCopy);

    int result_code = EVP_MD_CTX_copy_ex(ctxCopy, ctx);
    if (result_code == 0) {
        KAE_ThrowFromOpenssl(env, "EVP_MD_CTX_copy_ex failed", KAE_ThrowRuntimeException);
        goto cleanup;
    }
    KAE_TRACE("KAEDigest_nativeClone EVP_MD_CTX_copy_ex(ctxCopy = %p, ctx = %p) success", ctxCopy, ctx);
    KAE_TRACE("KAEDigest_nativeClone: finished");
    return (jlong) ctxCopy;

cleanup:
    EVP_MD_CTX_destroy(ctxCopy);
    return 0;
}

/*
 * Class:     org_openeuler_security_openssl_KAEDigest
 * Method:    nativeFree
 * Signature: (J)V
 */
JNIEXPORT void JNICALL
Java_org_openeuler_security_openssl_KAEDigest_nativeFree(JNIEnv *env, jclass cls, jlong ctxAddress)
{
    EVP_MD_CTX* ctx = (EVP_MD_CTX*) ctxAddress;
    KAE_TRACE("KAEDigest_nativeFree(ctx = %p)", ctx);
    if (ctx != NULL) {
        EVP_MD_CTX_destroy(ctx);
    }

    KAE_TRACE("KAEDigest_nativeFree: finished");
}
