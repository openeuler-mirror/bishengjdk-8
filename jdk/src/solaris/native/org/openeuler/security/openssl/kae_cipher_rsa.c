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

#include <openssl/rsa.h>
#include <openssl/evp.h>
#include "kae_util.h"
#include "kae_exception.h"
#include "org_openeuler_security_openssl_KAERSACipher.h"

#define SUCCESS 1
#define FAILED -1

typedef int RSACryptOperation(int, const unsigned char*, unsigned char*, RSA*, int);

typedef int EvpPkeyCryptOperation(EVP_PKEY_CTX*, unsigned char*, size_t*, const unsigned char*, size_t);

typedef int EvpPkeyCryptInitOperation(EVP_PKEY_CTX*);

/*
 * RSA encrypt or decrypt for NoPadding or PKCS1Padding , follow the steps below
 *
 */
static int RSACryptNotOAEPPadding(JNIEnv* env, jlong keyAddress, jint inLen, jbyteArray in, jbyteArray out,
    jint paddingType, RSACryptOperation rsaCryptOperation, char* cryptName) {
    jbyte* inBytes = NULL;
    jbyte* outBytes = NULL;
    int resultSize = 0;

    // get RSA
    EVP_PKEY* pkey = (EVP_PKEY*)keyAddress;

    // rsa = pkey->rsa
    RSA* rsa = EVP_PKEY_get1_RSA(pkey);
    if (rsa == NULL) {
        KAE_ThrowFromOpenssl(env, "EVP_PKEY_get1_RSA", KAE_ThrowRuntimeException);
        return 0;
    }

    // do encrypt or decrypt
    inBytes = (*env)->GetByteArrayElements(env, in, NULL);
    if (inBytes == NULL) {
        KAE_ThrowNullPointerException(env, "GetByteArrayElements failed");
        goto cleanup;
    }
    outBytes = (*env)->GetByteArrayElements(env, out, NULL);
    if (outBytes == NULL) {
        KAE_ThrowNullPointerException(env, "GetByteArrayElements failed");
        goto cleanup;
    }
    resultSize = rsaCryptOperation(inLen, (unsigned char*)inBytes, (unsigned char*)outBytes, rsa, paddingType);
    if (resultSize <= 0) {
        KAE_ThrowFromOpenssl(env, cryptName, KAE_ThrowBadPaddingException);
        goto cleanup;
    }
    jsize outLen = (*env)->GetArrayLength(env, out);
    (*env)->SetByteArrayRegion(env, out, 0, outLen, outBytes);

cleanup:
    if (outBytes != NULL) {
        (*env)->ReleaseByteArrayElements(env, out, outBytes, 0);
    }
    if (inBytes != NULL) {
        (*env)->ReleaseByteArrayElements(env, in, inBytes, 0);
    }
    return resultSize;
}

/*
 * set rsa padding
 */
static int SetRSAPadding(JNIEnv* env, EVP_PKEY_CTX* pkeyCtx, int paddingType) {
    if (EVP_PKEY_CTX_set_rsa_padding(pkeyCtx, paddingType) <= 0) {
        KAE_ThrowFromOpenssl(env, "EVP_PKEY_CTX_set_rsa_padding", KAE_ThrowInvalidAlgorithmParameterException);
        return FAILED;
    }
    return SUCCESS;
}

/*
 * set rsa mgf1 md
 */
static int SetRSAMgf1Md(JNIEnv* env, EVP_PKEY_CTX* pkeyCtx, const char* mgf1MdAlgoUTF) {
    EVP_MD* mgf1MD = (EVP_MD*)EVP_get_digestbyname(mgf1MdAlgoUTF);
    if (mgf1MD == NULL) {
        KAE_ThrowFromOpenssl(env, "EVP_get_digestbyname", KAE_ThrowInvalidAlgorithmParameterException);
        return FAILED;
    }
    if (EVP_PKEY_CTX_set_rsa_mgf1_md(pkeyCtx, mgf1MD) <= 0) {
        KAE_ThrowFromOpenssl(env, "EVP_PKEY_CTX_set_rsa_mgf1_md", KAE_ThrowInvalidAlgorithmParameterException);
        return FAILED;
    }
    return SUCCESS;
}

/*
 * set rsa oaep md
 */
static int SetRSAOaepMd(JNIEnv* env, EVP_PKEY_CTX* pkeyCtx, const char* oaepMdAlgoUTF) {
    EVP_MD* oaepMD = (EVP_MD*)EVP_get_digestbyname(oaepMdAlgoUTF);
    if (oaepMD == NULL) {
        KAE_ThrowFromOpenssl(env, "EVP_get_digestbyname", KAE_ThrowInvalidAlgorithmParameterException);
        return FAILED;
    }
    if (EVP_PKEY_CTX_set_rsa_oaep_md(pkeyCtx, oaepMD) <= 0) {
        KAE_ThrowFromOpenssl(env, "EVP_PKEY_CTX_set_rsa_oaep_md", KAE_ThrowInvalidAlgorithmParameterException);
        return FAILED;
    }
    return SUCCESS;
}

/*
 * set rsa oaep label
 */
static int SetRSAOaepLabel(JNIEnv* env, EVP_PKEY_CTX* pkeyCtx, jbyte* labelBytes, jsize labelSize) {
    if (EVP_PKEY_CTX_set0_rsa_oaep_label(pkeyCtx, labelBytes, labelSize) <= 0) {
        KAE_ThrowFromOpenssl(env, "EVP_PKEY_CTX_set0_rsa_oaep_label", KAE_ThrowInvalidAlgorithmParameterException);
        return FAILED;
    }
    return SUCCESS;
}

/*
 * release rsa oaep temp resource
 */
static void ReleaseRSACryptOAEPResource(JNIEnv* env, EVP_PKEY_CTX* pkeyCtx,
    jstring mgf1MdAlgo, const char* mgf1MdAlgoUTF, jstring oaepMdAlgo, const char* oaepMdAlgoUTF,
    jbyteArray in, jbyte* inBytes, jbyteArray out, jbyte* outBytes) {
    if (mgf1MdAlgoUTF != NULL) {
        (*env)->ReleaseStringUTFChars(env, mgf1MdAlgo, mgf1MdAlgoUTF);
    }
    if (oaepMdAlgoUTF != NULL) {
        (*env)->ReleaseStringUTFChars(env, oaepMdAlgo, oaepMdAlgoUTF);
    }
    if (outBytes != NULL) {
        (*env)->ReleaseByteArrayElements(env, out, outBytes, 0);
    }
    if (inBytes != NULL) {
        (*env)->ReleaseByteArrayElements(env, in, inBytes, 0);
    }
    EVP_PKEY_CTX_free(pkeyCtx);
}

static int RSACryptOAEPPadding(JNIEnv* env, jlong keyAddress, jint inLen, jbyteArray in, jbyteArray out,
    jint paddingType, jstring oaepMdAlgo, jstring mgf1MdAlgo, jbyteArray label,
    EvpPkeyCryptInitOperation cryptInitOperation, char* cryptInitName,
    EvpPkeyCryptOperation cryptOperation, char* cryptName) {
    EVP_PKEY_CTX* pkeyCtx = NULL;
    const char* mgf1MdAlgoUTF = NULL;
    const char* oaepMdAlgoUTF = NULL;
    jbyte* labelBytes = NULL;
    jbyte* outBytes = NULL;
    jbyte* inBytes = NULL;
    // outLen type should be size_t
    // EVP_PKEY_encrypt takes the outLen address as a parameter, and the parameter type is size_t*
    size_t outLen = 0;

    EVP_PKEY* pkey = (EVP_PKEY*) keyAddress;

    // new ctx
    // rsa encrypt/decrypt init
    if ((pkeyCtx = EVP_PKEY_CTX_new(pkey, NULL)) == NULL || cryptInitOperation(pkeyCtx) <= 0) {
        KAE_ThrowFromOpenssl(env, pkeyCtx == NULL ? "EVP_PKEY_CTX_new" : cryptInitName, KAE_ThrowInvalidKeyException);
        goto cleanup;
    }

    if ((mgf1MdAlgoUTF = (*env)->GetStringUTFChars(env, mgf1MdAlgo, 0)) == NULL ||
        (oaepMdAlgoUTF = (*env)->GetStringUTFChars(env, oaepMdAlgo, 0)) == NULL) {
        KAE_ThrowOOMException(env, "GetStringUTFChars failed");
        goto cleanup;
    }

    /*
     * set padding type
     * set rsa mgf1 md
     * set rsa oaep md
     */
    if(SetRSAPadding(env, pkeyCtx, paddingType) == FAILED ||
       SetRSAMgf1Md(env, pkeyCtx, mgf1MdAlgoUTF) == FAILED ||
       SetRSAOaepMd(env, pkeyCtx, oaepMdAlgoUTF) == FAILED) {
        goto cleanup;
    }

    // set rsa oaep label
    jsize labelSize = (*env)->GetArrayLength(env, label);
    if (labelSize > 0) {
        // EVP_PKEY_CTX_free will free the labelBytes, so we can not free labelBytes when cleanup.
        // Only SetRSAOaepLabel failed , free labelBytes.
        if ((labelBytes = malloc(labelSize)) == NULL) {
            KAE_ThrowNullPointerException(env, "malloc failed");
            goto cleanup;
        }
        (*env)->GetByteArrayRegion(env, label, 0, labelSize, labelBytes);
        if(SetRSAOaepLabel(env, pkeyCtx, labelBytes, labelSize) == FAILED) {
            free(labelBytes);
            goto cleanup;
        }
    }

    // do encrypt/decrypt
    outLen = (size_t)(*env)->GetArrayLength(env, out);
    if ((outBytes = (*env)->GetByteArrayElements(env, out, NULL)) == NULL ||
        (inBytes = (*env)->GetByteArrayElements(env, in, NULL)) == NULL) {
        KAE_ThrowNullPointerException(env, "GetByteArrayElements failed");
        goto cleanup;
    }
    if (cryptOperation(pkeyCtx, (unsigned char*)outBytes, &outLen, (unsigned char*)inBytes, inLen) <= 0) {
        KAE_ThrowFromOpenssl(env, cryptName, KAE_ThrowBadPaddingException);
        goto cleanup;
    }
    (*env)->SetByteArrayRegion(env, out, 0, outLen, outBytes);

cleanup:
    ReleaseRSACryptOAEPResource(env, pkeyCtx, mgf1MdAlgo, mgf1MdAlgoUTF, oaepMdAlgo, oaepMdAlgoUTF,
        in, inBytes, out, outBytes);
    return outLen;
}

/*
 * Release rsa param n,e,d,p,q,dmp1,dmq1,iqmp
 */
void ReleaseRSAParams(BIGNUM* bnN, BIGNUM* bnE, BIGNUM* bnD, BIGNUM* bnP, BIGNUM* bnQ,
                      BIGNUM* bnDMP1, BIGNUM* bnDMQ1, BIGNUM* bnIQMP) {
    KAE_ReleaseBigNumFromByteArray(bnN);
    KAE_ReleaseBigNumFromByteArray(bnE);
    KAE_ReleaseBigNumFromByteArray(bnD);
    KAE_ReleaseBigNumFromByteArray(bnP);
    KAE_ReleaseBigNumFromByteArray(bnQ);
    KAE_ReleaseBigNumFromByteArray(bnDMP1);
    KAE_ReleaseBigNumFromByteArray(bnDMQ1);
    KAE_ReleaseBigNumFromByteArray(bnIQMP);
}

/*
 * Create rsa private crt key
 * Class:     org_openeuler_security_openssl_KAERSACipher
 * Method:    nativeCreateRSAPrivateCrtKey
 * Signature: ([B[B[B[B[B[B[B[B)J
 */
JNIEXPORT jlong JNICALL Java_org_openeuler_security_openssl_KAERSACipher_nativeCreateRSAPrivateCrtKey(JNIEnv* env,
    jclass cls, jbyteArray n, jbyteArray e, jbyteArray d, jbyteArray p, jbyteArray q,
    jbyteArray dmp1, jbyteArray dmq1, jbyteArray iqmp) {
    BIGNUM* bnN = NULL;
    BIGNUM* bnE = NULL;
    BIGNUM* bnD = NULL;
    BIGNUM* bnP = NULL;
    BIGNUM* bnQ = NULL;
    BIGNUM* bnDMP1 = NULL;
    BIGNUM* bnDMQ1 = NULL;
    BIGNUM* bnIQMP = NULL;
    RSA* rsa = NULL;
    EVP_PKEY* pkey = NULL;

    // convert to big num
    if ((bnN = KAE_GetBigNumFromByteArray(env, n)) == NULL ||
        (bnE = KAE_GetBigNumFromByteArray(env, e)) == NULL ||
        (bnD = KAE_GetBigNumFromByteArray(env, d)) == NULL ||
        (bnP = KAE_GetBigNumFromByteArray(env, p)) == NULL ||
        (bnQ = KAE_GetBigNumFromByteArray(env, q)) == NULL ||
        (bnDMP1 = KAE_GetBigNumFromByteArray(env, dmp1)) == NULL ||
        (bnDMQ1 = KAE_GetBigNumFromByteArray(env, dmq1)) == NULL ||
        (bnIQMP = KAE_GetBigNumFromByteArray(env, iqmp)) == NULL) {
        goto err;
    }

    // new pkey
    pkey = EVP_PKEY_new();
    if (pkey == NULL) {
        KAE_ThrowFromOpenssl(env, "EVP_PKEY_new", KAE_ThrowRuntimeException);
        goto err;
    }

    // new rsa
    rsa = RSA_new();
    if (rsa == NULL) {
        KAE_ThrowFromOpenssl(env, "RSA_new", KAE_ThrowRuntimeException);
        goto err;
    }

    // set rsa private crt key params n,e,d,p,q,dmp1,dmp1,iqmp
    if (RSA_set0_key(rsa, bnN, bnE, bnD) <= 0 ||
        RSA_set0_factors(rsa, bnP, bnQ) <= 0 ||
        RSA_set0_crt_params(rsa, bnDMP1, bnDMQ1, bnIQMP) <= 0) {
        KAE_ThrowFromOpenssl(env, "RSA set param", KAE_ThrowRuntimeException);
        goto err;
    }

    // assign rsa to pkey
    int result = EVP_PKEY_assign_RSA(pkey, rsa);
    if (result <= 0) {
        KAE_ThrowFromOpenssl(env, "EVP_PKEY_assign_RSA", KAE_ThrowRuntimeException);
        goto err;
    }
    return (jlong)pkey;
err:
    ReleaseRSAParams(bnN, bnE, bnD, bnP, bnQ, bnDMP1, bnDMQ1, bnIQMP);
    RSA_free(rsa);
    EVP_PKEY_free(pkey);
    return 0;
}

/*
 * Create rsa public key
 * Class:     org_openeuler_security_openssl_KAERSACipher
 * Method:    nativeCreateRSAPublicKey
 * Signature: ([B[B)J
 */
JNIEXPORT jlong JNICALL Java_org_openeuler_security_openssl_KAERSACipher_nativeCreateRSAPublicKey(
    JNIEnv* env, jclass cls, jbyteArray n, jbyteArray e) {
    BIGNUM* bnN = NULL;
    BIGNUM* bnE = NULL;
    RSA* rsa = NULL;
    EVP_PKEY* pkey = NULL;

    // get public key param n
    bnN = KAE_GetBigNumFromByteArray(env, n);
    if (bnN == NULL) {
        goto err;
    }

    // get public key param e
    bnE = KAE_GetBigNumFromByteArray(env, e);
    if (bnE == NULL) {
        goto err;
    }

    // new RSA
    rsa = RSA_new();
    if (rsa == NULL) {
        KAE_ThrowFromOpenssl(env, "RSA_new", KAE_ThrowRuntimeException);
        goto err;
    }

    // new EVP_PKEY
    pkey = EVP_PKEY_new();
    if (pkey == NULL) {
        KAE_ThrowFromOpenssl(env, "EVP_PKEY_new", KAE_ThrowRuntimeException);
        goto err;
    }

    // set rsa public key params n and e
    if(RSA_set0_key(rsa, bnN, bnE, NULL) <= 0) {
        KAE_ThrowFromOpenssl(env, "RSA_set0_key", KAE_ThrowRuntimeException);
        goto err;
    }

    // assign rsa to pkey
    int result = EVP_PKEY_assign_RSA(pkey, rsa);
    if (result <= 0) {
        KAE_ThrowFromOpenssl(env, "EVP_PKEY_assign_RSA", KAE_ThrowRuntimeException);
        goto err;
    }
    return (jlong)pkey;
err:
    KAE_ReleaseBigNumFromByteArray(bnN);
    KAE_ReleaseBigNumFromByteArray(bnE);
    RSA_free(rsa);
    EVP_PKEY_free(pkey);
    return 0;
}

/*
 * Class:     org_openeuler_security_openssl_KAERSACipher
 * Method:    nativeRSAPrivateEncrypt
 * Signature: (JI[B[BI)I
 */
JNIEXPORT jint JNICALL Java_org_openeuler_security_openssl_KAERSACipher_nativeRSAPrivateEncrypt(JNIEnv* env,
    jclass cls, jlong keyAddress, jint inLen, jbyteArray in, jbyteArray out, jint paddingType) {
    return RSACryptNotOAEPPadding(env, keyAddress, inLen, in, out, paddingType, RSA_private_encrypt,
                                  "RSA_private_encrypt");
}

/*
 * Class:     org_openeuler_security_openssl_KAERSACipher
 * Method:    nativeRSAPrivateDecrypt
 * Signature: (JI[B[BI)I
 */
JNIEXPORT jint JNICALL Java_org_openeuler_security_openssl_KAERSACipher_nativeRSAPrivateDecrypt(JNIEnv* env,
    jclass cls, jlong keyAddress, jint inLen, jbyteArray in, jbyteArray out, jint paddingType) {
    return RSACryptNotOAEPPadding(env, keyAddress, inLen, in, out, paddingType, RSA_private_decrypt,
                                  "RSA_private_decrypt");
}

/*
 * Class:     org_openeuler_security_openssl_KAERSACipher
 * Method:    nativeRSAPublicEncrypt
 * Signature: (JI[B[BI)I
 */
JNIEXPORT jint JNICALL Java_org_openeuler_security_openssl_KAERSACipher_nativeRSAPublicEncrypt(JNIEnv* env,
    jclass cls, jlong keyAddress, jint inLen, jbyteArray in, jbyteArray out, jint paddingType) {
    return RSACryptNotOAEPPadding(env, keyAddress, inLen, in, out, paddingType, RSA_public_encrypt,
                                  "RSA_public_encrypt");
}

/*
 * Class:     org_openeuler_security_openssl_KAERSACipher
 * Method:    nativeRSAPublicDecrypt
 * Signature: (JI[B[BI)I
 */
JNIEXPORT jint JNICALL Java_org_openeuler_security_openssl_KAERSACipher_nativeRSAPublicDecrypt(JNIEnv* env,
    jclass cls, jlong keyAddress, jint inLen, jbyteArray in, jbyteArray out, jint paddingType) {
    return RSACryptNotOAEPPadding(env, keyAddress, inLen, in, out, paddingType, RSA_public_decrypt,
                                  "RSA_public_decrypt");
}

/*
 * Class:     org_openeuler_security_openssl_KAERSACipher
 * Method:    nativeRSAEncryptOAEPPading
 * Signature: (JI[B[BI[B[B[B)I
 */
JNIEXPORT jint JNICALL Java_org_openeuler_security_openssl_KAERSACipher_nativeRSAEncryptOAEPPadding(JNIEnv* env,
    jclass cls, jlong keyAddress, jint inLen, jbyteArray in, jbyteArray out,
    jint paddingType,jstring oaepMdAlgo, jstring mgf1MdAlgo, jbyteArray label) {
    return RSACryptOAEPPadding(env, keyAddress, inLen, in, out, paddingType, oaepMdAlgo, mgf1MdAlgo, label,
                               EVP_PKEY_encrypt_init, "EVP_PKEY_encrypt_init",
                               EVP_PKEY_encrypt, "EVP_PKEY_encrypt");
}

/*
 * Class:     org_openeuler_security_openssl_KAERSACipher
 * Method:    nativeRSADecryptOAEPPadding
 * Signature: (JI[B[BILjava/lang/String;Ljava/lang/String;[B)I
 */
JNIEXPORT jint JNICALL Java_org_openeuler_security_openssl_KAERSACipher_nativeRSADecryptOAEPPadding(JNIEnv* env,
    jclass cls, jlong keyAddress, jint inLen, jbyteArray in, jbyteArray out, jint paddingType,
    jstring oaepMdAlgo, jstring mgf1MdAlgo, jbyteArray label) {
    return RSACryptOAEPPadding(env, keyAddress, inLen, in, out, paddingType, oaepMdAlgo, mgf1MdAlgo, label,
                               EVP_PKEY_decrypt_init, "EVP_PKEY_decrypt_init",
                               EVP_PKEY_decrypt, "EVP_PKEY_decrypt");
}

/*
 * Class:     org_openeuler_security_openssl_KAERSACipher
 * Method:    nativeFreeKey
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_org_openeuler_security_openssl_KAERSACipher_nativeFreeKey(JNIEnv* env,
    jclass cls, jlong keyAddress) {
    EVP_PKEY* pkey = (EVP_PKEY*) keyAddress;
    if (pkey != NULL) {
        EVP_PKEY_free(pkey);
    }
}
