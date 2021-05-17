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
#include <stdbool.h>
#include <string.h>
#include <openssl/rsa.h>
#include <openssl/evp.h>
#include "kae_util.h"
#include "kae_exception.h"

// get EVP_MD by digestName
static const EVP_MD* getEvpMd(JNIEnv* env, jstring digestName) {
    const char* digestNameUtf = (*env)->GetStringUTFChars(env, digestName, 0);
    const EVP_MD* md = (EVP_MD*)EVP_get_digestbyname(digestNameUtf);
    (*env)->ReleaseStringUTFChars(env, digestName, digestNameUtf);
    if (md == NULL) {
        KAE_ThrowSignatureException(env, "Unsupported digest algorithm.");
    }
    return md;
}

// sign release
static void signRelease(JNIEnv* env, jbyteArray digestValue, jbyte* digestBytes, jbyte* sigBytes,
    EVP_PKEY_CTX* pkeyCtx) {
    if (digestBytes != NULL) {
        (*env)->ReleaseByteArrayElements(env, digestValue, digestBytes, 0);
    }
    if (sigBytes != NULL) {
        free(sigBytes);
    }
    if (pkeyCtx != NULL) {
        EVP_PKEY_CTX_free(pkeyCtx);
    }
}

// verify release
static void verifyRelease(JNIEnv* env, jbyteArray digestValue, jbyte* digestBytes, jbyteArray sigValue, jbyte* sigBytes,
    EVP_PKEY_CTX* pkeyCtx) {
    if (digestBytes != NULL) {
        (*env)->ReleaseByteArrayElements(env, digestValue, digestBytes, 0);
    }
    if (sigBytes != NULL) {
        (*env)->ReleaseByteArrayElements(env, sigValue, sigBytes, 0);
    }
    if (pkeyCtx != NULL) {
        EVP_PKEY_CTX_free(pkeyCtx);
    }
}

// set rsa PkeyCtx parameters
static bool setRsaPkeyCtxParameters(JNIEnv* env, EVP_PKEY_CTX* pkeyCtx, jint paddingType, jstring digestName) {
    // set rsa padding
    if (EVP_PKEY_CTX_set_rsa_padding(pkeyCtx, paddingType) <= 0) {
        KAE_ThrowFromOpenssl(env, "EVP_PKEY_CTX_set_rsa_padding", KAE_ThrowSignatureException);
        return false;
    }

    // set signature md
    const EVP_MD* md = getEvpMd(env, digestName);
    if (md == NULL) {
        return false;
    }

    if (EVP_PKEY_CTX_set_signature_md(pkeyCtx, md) <= 0) {
        KAE_ThrowFromOpenssl(env, "EVP_PKEY_CTX_set_signature_md", KAE_ThrowSignatureException);
        return false;
    }
    return true;
}

/*
 * Class:     org_openeuler_security_openssl_KAERSASignatureNative
 * Method:    rsaSign
 * Signature: (JLjava/lang/String;[BI)[B
 */
JNIEXPORT jbyteArray JNICALL Java_org_openeuler_security_openssl_KAERSASignatureNative_rsaSign(JNIEnv* env, jclass cls,
    jlong keyAddress, jstring digestName, jbyteArray digestValue, jint paddingType) {
    EVP_PKEY* pkey = (EVP_PKEY*)keyAddress;
    EVP_PKEY_CTX* pkeyCtx = NULL;
    jbyte* digestBytes = NULL;
    jbyte* sigBytes = NULL;
    jbyteArray sigByteArray = NULL;
    static ENGINE* kaeEngine = NULL;
    kaeEngine = (kaeEngine == NULL) ? GetKaeEngine() : kaeEngine;
    // new EVP_PKEY_CTX
    if ((pkeyCtx = EVP_PKEY_CTX_new(pkey, kaeEngine)) == NULL) {
        KAE_ThrowFromOpenssl(env, "EVP_PKEY_new", KAE_ThrowSignatureException);
        goto cleanup;
    }

    // sign init
    if (EVP_PKEY_sign_init(pkeyCtx) <= 0) {
        KAE_ThrowFromOpenssl(env, "EVP_PKEY_sign_init", KAE_ThrowSignatureException);
        goto cleanup;
    }

    // set rsa PkeyCtx parameters
    if (!setRsaPkeyCtxParameters(env, pkeyCtx, paddingType, digestName)) {
        goto cleanup;
    }

    // sign
    size_t sigLen = (size_t)EVP_PKEY_size(pkey);
    if (sigLen <= 0) {
        KAE_ThrowSignatureException(env, "The sigLen size cannot be zero or negative");
        goto cleanup;
    }
    if ((sigBytes = malloc(sigLen)) == NULL) {
        KAE_ThrowOOMException(env, "malloc failed");
        goto cleanup;
    }
    if ((digestBytes = (*env)->GetByteArrayElements(env, digestValue, NULL)) == NULL) {
        KAE_ThrowOOMException(env, "GetByteArrayElements failed");
        goto cleanup;
    }
    size_t digestLen = (size_t)(*env)->GetArrayLength(env, digestValue);
    if (EVP_PKEY_sign(pkeyCtx, (unsigned char*)sigBytes, &sigLen,
                      (const unsigned char*)digestBytes, digestLen) <= 0) {
        KAE_ThrowFromOpenssl(env, "EVP_PKEY_sign", KAE_ThrowSignatureException);
        goto cleanup;
    }

    // set signature byte to jbyteArray
    if ((sigByteArray = (*env)->NewByteArray(env, (jsize)sigLen)) == NULL) {
        KAE_ThrowOOMException(env, "NewByteArray failed");
        goto cleanup;
    }
    (*env)->SetByteArrayRegion(env, sigByteArray, 0, (jsize)sigLen, sigBytes);

cleanup:
    signRelease(env, digestValue, digestBytes, sigBytes, pkeyCtx);
    return sigByteArray;
}

/*
 * Class:     org_openeuler_security_openssl_KAERSASignatureNative
 * Method:    rsaVerify
 * Signature: (JLjava/lang/String;[BI[B)Z
 */
JNIEXPORT jboolean JNICALL Java_org_openeuler_security_openssl_KAERSASignatureNative_rsaVerify(JNIEnv* env, jclass cls,
    jlong keyAddress, jstring digestName, jbyteArray digestValue, jint paddingType, jbyteArray sigValue) {
    EVP_PKEY* pkey = (EVP_PKEY*)keyAddress;
    EVP_PKEY_CTX* pkeyCtx = NULL;
    jbyte* digestBytes = NULL;
    jbyte* sigBytes = NULL;
    jboolean isSuccess = JNI_FALSE;
    static ENGINE* kaeEngine = NULL;
    kaeEngine = (kaeEngine == NULL) ? GetKaeEngine() : kaeEngine;
    // new EVP_PKEY_CTX
    if ((pkeyCtx = EVP_PKEY_CTX_new(pkey, kaeEngine)) == NULL) {
        KAE_ThrowFromOpenssl(env, "EVP_PKEY_new", KAE_ThrowSignatureException);
        goto cleanup;
    }

    // verify init
    if (EVP_PKEY_verify_init(pkeyCtx) <= 0) {
        KAE_ThrowFromOpenssl(env, "EVP_PKEY_sign_init", KAE_ThrowSignatureException);
        goto cleanup;
    }

    // set rsa PkeyCtx parameters
    if (!setRsaPkeyCtxParameters(env, pkeyCtx, paddingType, digestName)) {
        goto cleanup;
    }

    // verify
    if ((digestBytes = (*env)->GetByteArrayElements(env, digestValue, NULL)) == NULL) {
        KAE_ThrowOOMException(env, "GetByteArrayElements failed");
        goto cleanup;
    }
    if ((sigBytes = (*env)->GetByteArrayElements(env, sigValue, NULL)) == NULL) {
        KAE_ThrowOOMException(env, "GetByteArrayElements failed");
        goto cleanup;
    }
    size_t sigLen = (size_t)(*env)->GetArrayLength(env, sigValue);
    size_t digestLen = (size_t)(*env)->GetArrayLength(env, digestValue);
    if (EVP_PKEY_verify(pkeyCtx, (const unsigned char*)sigBytes, sigLen,
                        (const unsigned char*)digestBytes, digestLen) <= 0) {
        KAE_ThrowFromOpenssl(env, "EVP_PKEY_verify", KAE_ThrowSignatureException);
        goto cleanup;
    }
    isSuccess = JNI_TRUE;

cleanup:
    verifyRelease(env, digestValue, digestBytes, sigValue, sigBytes, pkeyCtx);
    return isSuccess;
}

// set pss pkeyCtx parameters
static bool setPssPkeyCtxParameters(JNIEnv* env, EVP_PKEY_CTX* pkeyCtx, jint paddingType, jstring digestName,
    jstring mgf1DigestName, jint saltLen) {
    // set rsa padding
    if (EVP_PKEY_CTX_set_rsa_padding(pkeyCtx, paddingType) <= 0) {
        KAE_ThrowFromOpenssl(env, "EVP_PKEY_CTX_set_rsa_padding", KAE_ThrowSignatureException);
        return false;
    }

    // set signature md
    const EVP_MD* md = getEvpMd(env, digestName);
    if (md == NULL) {
        return false;
    }
    if (EVP_PKEY_CTX_set_signature_md(pkeyCtx, md) <= 0) {
        KAE_ThrowFromOpenssl(env, "EVP_PKEY_CTX_set_signature_md", KAE_ThrowSignatureException);
        return false;
    }

    // set rsa mgf1 md
    const EVP_MD* mgf1Md = getEvpMd(env, mgf1DigestName);
    if (mgf1Md == NULL) {
        return false;
    }
    if (EVP_PKEY_CTX_set_rsa_mgf1_md(pkeyCtx, mgf1Md) <= 0) {
        KAE_ThrowFromOpenssl(env, "EVP_PKEY_CTX_set_rsa_mgf1_md", KAE_ThrowSignatureException);
        return false;
    }

    // set salt len
    if (EVP_PKEY_CTX_set_rsa_pss_saltlen(pkeyCtx, saltLen) <= 0) {
        KAE_ThrowFromOpenssl(env, "EVP_PKEY_CTX_set_rsa_pss_saltlen", KAE_ThrowSignatureException);
        return false;
    }
    return true;
}

/*
 * Class:     org_openeuler_security_openssl_KAERSASignatureNative
 * Method:    pssSign
 * Signature: (JLjava/lang/String;[BILjava/lang/String;I)[B
 */
JNIEXPORT jbyteArray JNICALL Java_org_openeuler_security_openssl_KAERSASignatureNative_pssSign(JNIEnv* env, jclass cls,
    jlong keyAddress, jstring digestName, jbyteArray digestValue, jint paddingType, jstring mgf1DigestName,
    jint saltLen) {
    EVP_PKEY* pkey = (EVP_PKEY*)keyAddress;
    EVP_PKEY_CTX* pkeyCtx = NULL;
    jbyte* digestBytes = NULL;
    jbyte* sigBytes = NULL;
    jbyteArray sigByteArray = NULL;
    static ENGINE* kaeEngine = NULL;
    kaeEngine = (kaeEngine == NULL) ? GetKaeEngine() : kaeEngine;
    // new EVP_PKEY_CTX
    if ((pkeyCtx = EVP_PKEY_CTX_new(pkey, kaeEngine)) == NULL) {
        KAE_ThrowFromOpenssl(env, "EVP_PKEY_new", KAE_ThrowSignatureException);
        goto cleanup;
    }

    // sign init
    if (EVP_PKEY_sign_init(pkeyCtx) <= 0) {
        KAE_ThrowFromOpenssl(env, "EVP_PKEY_sign_init", KAE_ThrowSignatureException);
        goto cleanup;
    }

    // set pss pkeyCtx parameters
    if (!setPssPkeyCtxParameters(env, pkeyCtx, paddingType, digestName, mgf1DigestName, saltLen)) {
        goto cleanup;
    }

    // sign
    size_t sigLen = (size_t)EVP_PKEY_size(pkey);
    if (sigLen <= 0) {
        KAE_ThrowSignatureException(env, "The sigLen size cannot be zero or negative");
        goto cleanup;
    }
    if ((sigBytes = malloc(sigLen)) == NULL) {
        KAE_ThrowOOMException(env, "malloc failed");
        goto cleanup;
    }
    if ((digestBytes = (*env)->GetByteArrayElements(env, digestValue, NULL)) == NULL) {
        KAE_ThrowOOMException(env, "GetByteArrayElements failed");
        goto cleanup;
    }
    size_t digestLen = (size_t)(*env)->GetArrayLength(env, digestValue);
    if (EVP_PKEY_sign(pkeyCtx, (unsigned char*)sigBytes, &sigLen,
                      (const unsigned char*)digestBytes, digestLen) <= 0) {
        KAE_ThrowFromOpenssl(env, "EVP_PKEY_sign", KAE_ThrowSignatureException);
        goto cleanup;
    }

    // set signature byte to jbyteArray
    if ((sigByteArray = (*env)->NewByteArray(env, (jsize)sigLen)) == NULL) {
        KAE_ThrowOOMException(env, "NewByteArray failed");
        goto cleanup;
    }
    (*env)->SetByteArrayRegion(env, sigByteArray, 0, (jsize)sigLen, sigBytes);

cleanup:
    signRelease(env, digestValue, digestBytes, sigBytes, pkeyCtx);
    return sigByteArray;
}

/*
 * Class:     org_openeuler_security_openssl_KAERSASignatureNative
 * Method:    pssVerify
 * Signature: (JLjava/lang/String;[BILjava/lang/String;I[B)Z
 */
JNIEXPORT jboolean JNICALL Java_org_openeuler_security_openssl_KAERSASignatureNative_pssVerify(JNIEnv* env, jclass cls,
    jlong keyAddress, jstring digestName, jbyteArray digestValue, jint paddingType, jstring mgf1DigestName,
    jint saltLen, jbyteArray sigValue) {
    EVP_PKEY* pkey = (EVP_PKEY*)keyAddress;
    EVP_PKEY_CTX* pkeyCtx = NULL;
    jbyte* digestBytes = NULL;
    jbyte* sigBytes = NULL;
    jboolean isSuccess = JNI_FALSE;
    static ENGINE* kaeEngine = NULL;
    kaeEngine = (kaeEngine == NULL) ? GetKaeEngine() : kaeEngine;
    // new EVP_PKEY_CTX
    if ((pkeyCtx = EVP_PKEY_CTX_new(pkey, kaeEngine)) == NULL) {
        KAE_ThrowFromOpenssl(env, "EVP_PKEY_new", KAE_ThrowSignatureException);
        goto cleanup;
    }

    // verify init
    if (EVP_PKEY_verify_init(pkeyCtx) <= 0) {
        KAE_ThrowFromOpenssl(env, "EVP_PKEY_sign_init", KAE_ThrowSignatureException);
        goto cleanup;
    }

    // set pkeyCtx parameters
    if (!setPssPkeyCtxParameters(env, pkeyCtx, paddingType, digestName, mgf1DigestName, saltLen)) {
        goto cleanup;
    }

    // verify
    if ((digestBytes = (*env)->GetByteArrayElements(env, digestValue, NULL)) == NULL) {
        KAE_ThrowOOMException(env, "GetByteArrayElements failed");
        goto cleanup;
    }
    if ((sigBytes = (*env)->GetByteArrayElements(env, sigValue, NULL)) == NULL) {
        KAE_ThrowOOMException(env, "GetByteArrayElements failed");
        goto cleanup;
    }
    size_t sigLen = (size_t)(*env)->GetArrayLength(env, sigValue);
    size_t digestLen = (size_t)(*env)->GetArrayLength(env, digestValue);
    if (EVP_PKEY_verify(pkeyCtx, (const unsigned char*)sigBytes, sigLen,
                        (const unsigned char*)digestBytes, digestLen) <= 0) {
        KAE_ThrowFromOpenssl(env, "EVP_PKEY_verify", KAE_ThrowSignatureException);
        goto cleanup;
    }
    isSuccess = JNI_TRUE;

cleanup:
    verifyRelease(env, digestValue, digestBytes, sigValue, sigBytes, pkeyCtx);
    return isSuccess;
}