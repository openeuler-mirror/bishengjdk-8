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

#include <openssl/evp.h>
#include <openssl/ec.h>
#include <stdbool.h>
#include <string.h>
#include "kae_util.h"
#include "kae_log.h"
#include "kae_exception.h"
#include "org_openeuler_security_openssl_KAESM2Cipher.h"
#include "ssl_utils.h"

static void FreeSM2KeyParam(BIGNUM* bn_x, BIGNUM* bn_y, BIGNUM* bn_key, EC_GROUP* group, EC_POINT* pt)
{
    if (bn_x != NULL) {
        KAE_ReleaseBigNumFromByteArray(bn_x);
    }
    if (bn_y != NULL) {
        KAE_ReleaseBigNumFromByteArray(bn_y);
    }
    if (bn_key != NULL) {
        KAE_ReleaseBigNumFromByteArray_Clear(bn_key);
    }
    if (group != NULL) {
        SSL_UTILS_EC_GROUP_free(group);
    }
    if (pt != NULL) {
        SSL_UTILS_EC_POINT_free(pt);
    }
}

/*
 * SM2 encrypt or decrypt, follow the steps below
 */
static jbyteArray SM2_Crypt(JNIEnv *env, jlong keyAddress, jbyteArray inArr, jint inLen, bool isEncrypt) {
    unsigned char* inbytes = NULL;
    unsigned char* outbytes = NULL;
    size_t outLen = 0;
    jbyteArray outArr = NULL;
    EVP_PKEY* pkey = NULL;
    EVP_PKEY_CTX* ctx = NULL;
    ENGINE* kaeEngine = NULL;

    // init Engine
    kaeEngine = GetEngineByAlgorithmIndex(SM2_INDEX);
    KAE_TRACE("SM2_Crypt: kaeEngine => %p", kaeEngine);

    if ((inbytes = (unsigned char*)malloc(inLen)) == NULL) {
        KAE_ThrowOOMException(env, "malloc failed");
        goto cleanup;
    }
    memset(inbytes, 0, inLen);

    // get inArr bytes
    (*env)->GetByteArrayRegion(env, inArr, 0, inLen, (jbyte*)inbytes);
    if (inbytes == NULL) {
        KAE_ThrowNullPointerException(env, "GetByteArrayRegion failed");
        goto cleanup;
    }

    pkey = (EVP_PKEY*) keyAddress;

    // new ctx
    if ((ctx = SSL_UTILS_EVP_PKEY_CTX_new(pkey, kaeEngine)) == NULL) {
        KAE_ThrowFromOpenssl(env, "EVP_PKEY_CTX_new", KAE_ThrowInvalidKeyException);
        goto cleanup;
    }
    
    // sm2 encrypt/decrypt init
    if (isEncrypt) {
        // init encrypt ctx
        if (SSL_UTILS_EVP_PKEY_encrypt_init(ctx) <= 0) {
            KAE_ThrowFromOpenssl(env, "EVP_PKEY_encrypt_init", KAE_ThrowRuntimeException);
            goto cleanup;
        }

        // calculated outArr length
        if (SSL_UTILS_EVP_PKEY_encrypt(ctx, NULL, &outLen, inbytes, inLen) <= 0) {
            KAE_ThrowFromOpenssl(env, "EVP_PKEY_encrypt failed. calculated outArr length", KAE_ThrowRuntimeException);
            goto cleanup;
        }
    }else {
        // init decrypt ctx
        if (SSL_UTILS_EVP_PKEY_decrypt_init(ctx) <= 0) {
            KAE_ThrowFromOpenssl(env, "EVP_PKEY_decrypt_init", KAE_ThrowRuntimeException);
            goto cleanup;
        }

        // calculated outArr length
        if (SSL_UTILS_EVP_PKEY_decrypt(ctx, NULL, &outLen, inbytes, inLen) <= 0) {
            KAE_ThrowFromOpenssl(env, "EVP_PKEY_decrypt failed. calculated outArr length", KAE_ThrowRuntimeException);
            goto cleanup;
        }
    }
    
    if ((outbytes = (unsigned char*)malloc(outLen)) == NULL) {
        KAE_ThrowOOMException(env, "malloc failed");
        goto cleanup;
    }
     memset(outbytes, 0, outLen);

    if (isEncrypt) {
        // sm2 encrypt dofinal
        if (SSL_UTILS_EVP_PKEY_encrypt(ctx, outbytes, &outLen, inbytes, inLen) <= 0) {
            KAE_ThrowFromOpenssl(env, "EVP_PKEY_encrypt failed. sm2 encrypt dofinal", KAE_ThrowRuntimeException);
            goto cleanup;
        }
    }else {
        // sm2 decrypt dofinal
        if (SSL_UTILS_EVP_PKEY_decrypt(ctx, outbytes, &outLen, inbytes, inLen) <= 0) {
            KAE_ThrowFromOpenssl(env, "EVP_PKEY_decrypt failed. sm2 decrypt dofinal", KAE_ThrowRuntimeException);
            goto cleanup;
        }
    }
    KAE_TRACE("SM2_Crypt: finished");
    
    if ((outArr = (*env)->NewByteArray(env, outLen)) == NULL) {
        KAE_ThrowNullPointerException(env, "NewByteArray failed");
        goto cleanup;
    }
    (*env)->SetByteArrayRegion(env, outArr, 0, outLen, (jbyte*)outbytes);
cleanup:
    if (inbytes != NULL) {
        memset(inbytes, 0, inLen);
        free(inbytes);
    }
    if (outbytes != NULL) {
        memset(outbytes, 0, outLen);
        free(outbytes);
    }
    SSL_UTILS_EVP_PKEY_CTX_free(ctx);
    return outArr;
}

/*
 * Class:     KAESM2Cipher
 * Method:    nativeCreateSM2PublicKey
 * Signature: ([B[B)J
 */
JNIEXPORT jlong JNICALL Java_org_openeuler_security_openssl_KAESM2Cipher_nativeCreateSM2PublicKey(JNIEnv *env,
    jclass cls, jbyteArray xArr, jbyteArray yArr) {
    BIGNUM* bn_x = NULL;
    BIGNUM* bn_y = NULL;
    EC_GROUP* group = NULL;
    EC_POINT* pubkey_pt = NULL;
    EC_KEY* eckey = NULL;
    EVP_PKEY* pkey = NULL;

    // convert to big num
    if ((bn_x = KAE_GetBigNumFromByteArray(env, xArr)) == NULL ||
        (bn_y = KAE_GetBigNumFromByteArray(env, yArr)) == NULL) {
        goto cleanup;
    }

    // new EC_GROUP by curve_name
    if ((group = SSL_UTILS_EC_GROUP_new_by_curve_name(NID_sm2)) == NULL) {
        KAE_ThrowFromOpenssl(env, "EC_GROUP_new_by_curve_name", KAE_ThrowRuntimeException);
        goto cleanup;
    }

    // new EC_POINT
    if((pubkey_pt = SSL_UTILS_EC_POINT_new(group)) == NULL) {
        KAE_ThrowFromOpenssl(env, "EC_POINT_new", KAE_ThrowRuntimeException);
        goto cleanup;
    }

    // set the x and y coordinates
    if(SSL_UTILS_EC_POINT_set_affine_coordinates_GFp(group, pubkey_pt, bn_x, bn_y, NULL) <= 0) {
        KAE_ThrowFromOpenssl(env, "EC_POINT_set_affine_coordinates_GFp", KAE_ThrowRuntimeException);
        goto cleanup;
    }

    // new EC_KEY
    if ((eckey = SSL_UTILS_EC_KEY_new_by_curve_name(NID_sm2)) == NULL) {
        KAE_ThrowFromOpenssl(env, "EC_KEY_new_by_curve_name", KAE_ThrowRuntimeException);
        goto cleanup;
    }
    // set ec_key by publickey_point
    if (SSL_UTILS_EC_KEY_set_public_key(eckey ,pubkey_pt) <= 0) {
        KAE_ThrowFromOpenssl(env, "EC_KEY_set_public_key", KAE_ThrowRuntimeException);
        goto cleanup;
    }

    // new EVP_PKEY
    if ((pkey = SSL_UTILS_EVP_PKEY_new()) == NULL) {
        KAE_ThrowFromOpenssl(env, "EVP_PKEY_new", KAE_ThrowRuntimeException);
        goto cleanup;
    }

    // set the pkey by the ec_key
    // Changed from macro, "EVP_PKEY_assign_EC_KEY(pkey,eckey)" is "EVP_PKEY_assign((pkey),EVP_PKEY_EC, (char *)(eckey))" in openssl 1 and 3
    if (SSL_UTILS_EVP_PKEY_assign((pkey),EVP_PKEY_EC, (char *)(eckey)) <= 0) {
        KAE_ThrowFromOpenssl(env, "EVP_PKEY_assign_EC_KEY", KAE_ThrowRuntimeException);
        goto cleanup;
    }

    // set the alias type of the key
    // TODO EVP_PKEY_set_alias_type is removed since openssl 3
    if (SSL_UTILS_EVP_PKEY_set_alias_type(env, pkey, EVP_PKEY_SM2) <= 0) {
        KAE_ThrowFromOpenssl(env, "EVP_PKEY_set_alias_type", KAE_ThrowRuntimeException);
        goto cleanup;
    }

    FreeSM2KeyParam(bn_x, bn_y, NULL, group, pubkey_pt);
    KAE_TRACE("KAESM2Cipher_nativeCreateSM2PublicKey: finished");
    return (jlong)pkey;
cleanup:
    FreeSM2KeyParam(bn_x, bn_y, NULL, group, pubkey_pt);
    if (eckey != NULL) {
        SSL_UTILS_EC_KEY_free(eckey);
    }
    if (pkey != NULL) {
        SSL_UTILS_EVP_PKEY_free(pkey);
    }
    return 0;
}

/*
 * Class:     KAESM2Cipher
 * Method:    nativeCreateSM2PrivateKey
 * Signature: ([B[B)J
 */
JNIEXPORT jlong JNICALL Java_org_openeuler_security_openssl_KAESM2Cipher_nativeCreateSM2PrivateKey(JNIEnv *env,
    jclass cls, jbyteArray keyArr, jboolean sign) {
    BIGNUM* bn_key = NULL;
    EC_KEY* eckey = NULL;
    EVP_PKEY* pkey = NULL;
    EC_GROUP* group = NULL;
    EC_POINT* pt = NULL;

    // convert to big num
    if ((bn_key = KAE_GetBigNumFromByteArray(env, keyArr)) == NULL) {
        goto cleanup;
    } 

    // new EC_KEY
    if ((eckey = SSL_UTILS_EC_KEY_new_by_curve_name(NID_sm2)) == NULL) {
        KAE_ThrowFromOpenssl(env, "EC_KEY_new_by_curve_name", KAE_ThrowRuntimeException);
        goto cleanup;
    }
    
    // set the ec_key by bn_key
    if ((SSL_UTILS_EC_KEY_set_private_key(eckey ,bn_key)) <= 0) {
        KAE_ThrowFromOpenssl(env, "EC_KEY_set_private_key", KAE_ThrowRuntimeException);
        goto cleanup;
    }
    
    // new group by curve_name
    if ((group = SSL_UTILS_EC_GROUP_new_by_curve_name(NID_sm2)) == NULL) {
        KAE_ThrowFromOpenssl(env, "EC_GROUP_new_by_curve_name", KAE_ThrowRuntimeException);
        goto cleanup;
    }

    if (sign) {
        // new EC_POINT
        if ((pt = SSL_UTILS_EC_POINT_new(group)) == NULL) {
            KAE_ThrowFromOpenssl(env, "EC_POINT_new", KAE_ThrowRuntimeException);
            goto cleanup;
        }
        // calculation of EC_POINT by EC_POINT_mul functions
        if (SSL_UTILS_EC_POINT_mul(group, pt, bn_key, NULL, NULL, NULL) <= 0) {
            KAE_ThrowFromOpenssl(env, "EC_POINT_mul", KAE_ThrowRuntimeException);
            goto cleanup;
        }
        // set ec_key by ec_point
        if (SSL_UTILS_EC_KEY_set_public_key(eckey ,pt) <= 0) {
            KAE_ThrowFromOpenssl(env, "EC_KEY_set_public_key", KAE_ThrowRuntimeException);
            goto cleanup;
        }
    }

    // new EVP_PKEY
    if ((pkey = SSL_UTILS_EVP_PKEY_new()) == NULL) {
        KAE_ThrowFromOpenssl(env, "EVP_PKEY_new", KAE_ThrowRuntimeException);
        goto cleanup;
    }

    // set the pkey by the ec_key
    // Changed from macro, "EVP_PKEY_assign_EC_KEY(pkey,eckey)" is "EVP_PKEY_assign((pkey),EVP_PKEY_EC, (char *)(eckey))" in openssl 1 and 3
    if (SSL_UTILS_EVP_PKEY_assign_EC_KEY(pkey , eckey) <= 0) {
        KAE_ThrowFromOpenssl(env, "EVP_PKEY_assign_EC_KEY", KAE_ThrowRuntimeException);
        goto cleanup;
    }

    // set the alias type of the key
    // TODO EVP_PKEY_set_alias_type is removed since openssl 3
    if (SSL_UTILS_EVP_PKEY_set_alias_type(env, pkey, EVP_PKEY_SM2) <= 0) {
        KAE_ThrowFromOpenssl(env, "EVP_PKEY_set_alias_type", KAE_ThrowRuntimeException);
        goto cleanup;
    }

    FreeSM2KeyParam(NULL, NULL, bn_key, group, pt);
    KAE_TRACE("KAESM2Cipher_nativeCreateSM2PrivateKey: finished");
    return (jlong)pkey;
cleanup:
    FreeSM2KeyParam(NULL, NULL, bn_key, group, pt);
    if (eckey != NULL) {
        SSL_UTILS_EC_KEY_free(eckey);
    }
    if (pkey != NULL) {
        SSL_UTILS_EVP_PKEY_free(pkey);
    }
    return 0;
}

/*
 * Class:     KAESM2Cipher
 * Method:    nativeFreeKey
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_org_openeuler_security_openssl_KAESM2Cipher_nativeFreeKey(JNIEnv *env,
    jclass cls, jlong keyAddress) {
    KAE_TRACE("KAESM2Cipher_nativeFreeKey(keyAddress = %p)", keyAddress);

    if(keyAddress == 0){
        KAE_ThrowInvalidKeyException(env, "nativeFreeKey failed. keyAddress is Invalid");
        return;
    }
    EVP_PKEY* pkey = (EVP_PKEY*) keyAddress;
    if (pkey != NULL) {
        SSL_UTILS_EVP_PKEY_free(pkey);
    }

    KAE_TRACE("KAESM2Cipher_nativeFreeKey: finished");
}

/*
 * Class:     KAESM2Cipher
 * Method:    nativeSM2Encrypt
 * Signature: (J[BI)[B
 */
JNIEXPORT jbyteArray JNICALL Java_org_openeuler_security_openssl_KAESM2Cipher_nativeSM2Encrypt(JNIEnv *env,
    jclass cls, jlong keyAddress, jbyteArray inArr, jint inLen) {
    KAE_TRACE("KAESM2Cipher_nativeSM2Encrypt(keyAddress = %p, inArr = %p, inLen = %d)", keyAddress, inArr, inLen);
    return SM2_Crypt(env, keyAddress, inArr, inLen, true);
}

/*
 * Class:     KAESM2Cipher
 * Method:    nativeSM2Decrypt
 * Signature: (J[BI)[B
 */
JNIEXPORT jbyteArray JNICALL Java_org_openeuler_security_openssl_KAESM2Cipher_nativeSM2Decrypt(JNIEnv *env,
    jclass cls, jlong keyAddress, jbyteArray inArr, jint inLen) {
    KAE_TRACE("KAESM2Cipher_nativeSM2Decrypt(keyAddress = %p, inArr = %p, inLen = %d)", keyAddress, inArr, inLen);
    return SM2_Crypt(env, keyAddress, inArr, inLen, false);
}