/*
 * Copyright (c) 2024, Huawei Technologies Co., Ltd. All rights reserved.
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

#include "org_openeuler_security_openssl_KAESM2Signature.h"
#include "kae_util.h"
#include "kae_log.h"
#include "kae_exception.h"
#include "ssl_utils.h"
#include <openssl/evp.h>
#include <openssl/ec.h>
#include <stdbool.h>
#include <string.h>

static const EVP_MD* GetEVP_MDByName(JNIEnv *env, const char* algo)
{
    static const EVP_MD* sm3 = NULL;

    if (strcasecmp(algo, "SM3") == 0) {
        return sm3 == NULL ? sm3 = SSL_UTILS_EVP_sm3() : sm3;
    } else {
        KAE_ThrowRuntimeException(env, "GetEVP_MDByName error");
        return NULL;
    }
}

/*
* Class:     org_openeuler_security_openssl_KAESM2Signature
* Method:    nativeClone
* Signature: (J)J
*/
JNIEXPORT jlong JNICALL
Java_org_openeuler_security_openssl_KAESM2Signature_nativeClone(JNIEnv *env, jclass cls, jlong ctxAddress)
{
    EVP_MD_CTX* ctx = (EVP_MD_CTX*) ctxAddress;
    KAE_TRACE("KAESM2Signature_nativeClone: ctx = %p", ctx);
    if (ctx == NULL) {
        return 0;
    }

    // EVP_MD_CTX_create is macro in openssl 1 and 3
    EVP_MD_CTX* ctxCopy = SSL_UTILS_EVP_MD_CTX_create();
    if (ctxCopy == NULL) {
        KAE_ThrowOOMException(env, "create EVP_MD_CTX fail");
        return 0;
    }
    KAE_TRACE("KAESM2Signature_nativeClone: create ctxCopy => %p", ctxCopy);

    int result_code = SSL_UTILS_EVP_MD_CTX_copy_ex(ctxCopy, ctx);
    if (result_code == 0) {
        KAE_ThrowFromOpenssl(env, "EVP_MD_CTX_copy_ex failed", KAE_ThrowRuntimeException);
        goto cleanup;
    }
    KAE_TRACE("KAESM2Signature_nativeClone EVP_MD_CTX_copy_ex(ctxCopy = %p, ctx = %p) success", ctxCopy, ctx);
    KAE_TRACE("KAESM2Signature_nativeClone: finished");
    return (jlong) ctxCopy;

cleanup:
    if (ctxCopy != NULL) {
        SSL_UTILS_EVP_MD_CTX_free(ctxCopy);
    }
    return 0;
}

/*
 * Class:     org_openeuler_security_openssl_KAESM2Signature
 * Method:    nativeFreeSM2Ctx
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_org_openeuler_security_openssl_KAESM2Signature_nativeFreeSM2Ctx(JNIEnv *env,
    jclass cls, jlong ctxAddress)
{
    if(ctxAddress == 0){
        KAE_ThrowInvalidKeyException(env, "nativeFreeSM2Ctx failed. ctxAddress is Invalid");
    }
    EVP_MD_CTX *md_ctx = (EVP_MD_CTX*) ctxAddress;
    if (md_ctx != NULL) {
        SSL_UTILS_EVP_MD_CTX_free(md_ctx);
    }
    KAE_TRACE("KAESM2Signature_nativeFreeSM2Ctx: finished");
}
  
/*
 * Class:     org_openeuler_security_openssl_KAESM2Signature
 * Method:    nativeInitSM2Ctx
 * Signature: (JLjava/lang/String;Z)J
 */
JNIEXPORT jlong JNICALL Java_org_openeuler_security_openssl_KAESM2Signature_nativeInitSM2Ctx(JNIEnv *env,
    jclass cls, jlong keyAddress, jstring digestName, jstring id, jboolean isSign)
{
    EVP_MD_CTX* md_ctx = NULL;
    EVP_PKEY_CTX* pctx = NULL;
    EVP_PKEY* pkey = NULL;
    pkey = (EVP_PKEY*) keyAddress;
    ENGINE* kaeEngine = NULL;

    // init engine
    kaeEngine = GetEngineByAlgorithmIndex(SM2_INDEX);
    KAE_TRACE("KAESM2Signature_nativeInitSM2Ctx: kaeEngine => %p", kaeEngine);

    const char* algo = (*env)->GetStringUTFChars(env, digestName, 0);
    const char* sm2_id = (*env)->GetStringUTFChars(env, id, 0);

    // new pkey_ctx
    if ((pctx = SSL_UTILS_EVP_PKEY_CTX_new(pkey, kaeEngine)) == NULL) {
        KAE_ThrowFromOpenssl(env, "EVP_PKEY_CTX_new", KAE_ThrowInvalidKeyException);
        goto cleanup;
    }
    
    // EVP_PKEY_CTX_set1_id is macro in openssl 1
    if (SSL_UTILS_EVP_PKEY_CTX_set1_id(pctx, sm2_id, strlen(sm2_id)) <= 0) {
        KAE_ThrowFromOpenssl(env, "EVP_PKEY_CTX_set1_id", KAE_ThrowRuntimeException);
        goto cleanup;
    }

    // new md_ctx
    if ((md_ctx = SSL_UTILS_EVP_MD_CTX_new()) == NULL) {
        KAE_ThrowFromOpenssl(env, "EVP_MD_CTX_new", KAE_ThrowRuntimeException);
        goto cleanup;
    }

    // set pkey_ctx in md_ctx
    SSL_UTILS_EVP_MD_CTX_set_pkey_ctx(md_ctx, pctx);

    // init md_ctx
    if(isSign){
        if (SSL_UTILS_EVP_DigestSignInit(md_ctx, NULL, GetEVP_MDByName(env, algo), kaeEngine, pkey) <= 0) {
            KAE_ThrowFromOpenssl(env, "EVP_DigestSignInit", KAE_ThrowRuntimeException);
            goto cleanup;
        }
    }else {
        if (SSL_UTILS_EVP_DigestVerifyInit(md_ctx, NULL, GetEVP_MDByName(env, algo), kaeEngine, pkey) <= 0) {
            KAE_ThrowFromOpenssl(env, "EVP_DigestVerifyInit", KAE_ThrowRuntimeException);
            goto cleanup;
        }
    }
    (*env)->ReleaseStringUTFChars(env, digestName, algo);
    (*env)->ReleaseStringUTFChars(env, id, sm2_id);
    return (jlong)md_ctx;
cleanup:
    (*env)->ReleaseStringUTFChars(env, digestName, algo);
    (*env)->ReleaseStringUTFChars(env, id, sm2_id);
    if (pctx != NULL) {
        SSL_UTILS_EVP_PKEY_CTX_free(pctx);
    }
    if (md_ctx != NULL) {
        SSL_UTILS_EVP_MD_CTX_free(md_ctx);
    }
    return 0;
}

/*
 * Class:     org_openeuler_security_openssl_KAESM2Signature
 * Method:    nativeSM2Update
 * Signature: (J[BIZ)V
 */
JNIEXPORT void JNICALL Java_org_openeuler_security_openssl_KAESM2Signature_nativeSM2Update(JNIEnv *env,
    jclass cls, jlong ctxAddress, jbyteArray msgArr, jint msgLen, jboolean isSign)
{
    EVP_MD_CTX* md_ctx = NULL;
    unsigned char* msg = NULL;
    md_ctx = (EVP_MD_CTX*) ctxAddress;

    if ((msg = (unsigned char*)malloc(msgLen)) == NULL) {
        KAE_ThrowOOMException(env, "malloc error");
        goto cleanup;
    }
    memset(msg, 0, msgLen);

    (*env)->GetByteArrayRegion(env, msgArr, 0, msgLen, (jbyte*)msg);

    if(isSign){
        if (SSL_UTILS_EVP_DigestSignUpdate(md_ctx, msg, msgLen) <= 0) {
            KAE_ThrowFromOpenssl(env, "EVP_DigestSignUpdate", KAE_ThrowRuntimeException);
            goto cleanup;
        }
    }else {
        if (SSL_UTILS_EVP_DigestVerifyUpdate(md_ctx, msg, msgLen) <= 0) {
            KAE_ThrowFromOpenssl(env, "EVP_DigestVerifyUpdate", KAE_ThrowRuntimeException);
            goto cleanup;
        }
    }
    KAE_TRACE("KAESM2Signature_nativeSM2Update: finished");
cleanup:
    if (msg != NULL) {
        memset(msg, 0, msgLen);
        free(msg);
    }
}

/*
 * Class:     org_openeuler_security_openssl_KAESM2Signature
 * Method:    nativeSM2SignFinal
 * Signature: (J)[B
 */
JNIEXPORT jbyteArray JNICALL Java_org_openeuler_security_openssl_KAESM2Signature_nativeSM2SignFinal(JNIEnv *env,
    jclass cls, jlong ctxAddress)
{
    EVP_MD_CTX* md_ctx = NULL;
    unsigned char* sig = NULL;
    size_t sig_len = 0;
    jbyteArray sigByteArray = NULL;
    md_ctx = (EVP_MD_CTX*) ctxAddress;

    // determine the size of the signature
    if (SSL_UTILS_EVP_DigestSignFinal(md_ctx, NULL, &sig_len) <= 0) {
         KAE_ThrowFromOpenssl(env, "EVP_DigestSignFinal", KAE_ThrowRuntimeException);
        goto cleanup;
    }

    if ((sig = malloc(sig_len)) == NULL) {
        KAE_ThrowOOMException(env, "malloc error");
        goto cleanup;
    }
    memset(sig, 0, sig_len);

    // sign
    if (SSL_UTILS_EVP_DigestSignFinal(md_ctx, sig, &sig_len) <= 0) {
         KAE_ThrowFromOpenssl(env, "EVP_DigestSignFinal", KAE_ThrowSignatureException);
        goto cleanup;
    }

    if ((sigByteArray = (*env)->NewByteArray(env, sig_len)) == NULL) {
        goto cleanup;
    }
    (*env)->SetByteArrayRegion(env, sigByteArray, 0, sig_len, (jbyte*)sig);
    KAE_TRACE("KAESM2Signature_nativeSM2SignFinal: finished");
cleanup:
    if (sig != NULL) {
        memset(sig, 0, sig_len);
        free(sig);
    }
    return sigByteArray;
}

/*
 * Class:     org_openeuler_security_openssl_KAESM2Signature
 * Method:    nativeSM2VerifyFinal
 * Signature: (J[B)Z
 */
JNIEXPORT jboolean JNICALL Java_org_openeuler_security_openssl_KAESM2Signature_nativeSM2VerifyFinal(JNIEnv *env,
    jclass cls, jlong ctxAddress, jbyteArray sigBytesArr, jint sigLen)
{
    EVP_MD_CTX* md_ctx = NULL;
    unsigned char* sigBytes = NULL;
    jboolean isSuccess = JNI_FALSE;
    md_ctx = (EVP_MD_CTX*) ctxAddress;

    if ((sigBytes = (unsigned char*)malloc(sigLen)) == NULL) {
        KAE_ThrowOOMException(env, "malloc error");
        goto cleanup;
    }
    (*env)->GetByteArrayRegion(env, sigBytesArr, 0, sigLen, (jbyte*)sigBytes);

    // verify
    if (SSL_UTILS_EVP_DigestVerifyFinal(md_ctx, sigBytes, sigLen) <= 0) {
        KAE_ThrowFromOpenssl(env, "EVP_DigestVerifyFinal", KAE_ThrowSignatureException);
        goto cleanup;
    }
    isSuccess = JNI_TRUE;
cleanup:
    if (sigBytes != NULL) {
        memset(sigBytes, 0, sigLen);
        free(sigBytes);
    }
    return isSuccess;
}