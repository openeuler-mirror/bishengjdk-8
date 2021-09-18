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
#include <openssl/objects.h>
#include <openssl/ec.h>
#include "kae_util.h"
#include "kae_exception.h"
#include "kae_log.h"
#include "org_openeuler_security_openssl_KAEECKeyPairGenerator.h"

#define KAE_EC_PARAM_NUM_SIZE 7
#define KAE_EC_KEY_NUM_SIZE 3

// ECDH param index.
typedef enum ECDHParamIndex {
    ecdhP = 0,
    ecdhA,
    ecdhB,
    ecdhX,
    ecdhY,
    ecdhOrder,
    ecdhCofactor
} ECDHParamIndex;

// ECDH Key index.
typedef enum ECDHKeyIndex {
    ecdhWX = 0,
    ecdhWY,
    ecdhS
} ECDHKeyIndex;

static void FreeECDHCurveParam(JNIEnv* env, BIGNUM* p, BIGNUM* a, BIGNUM* b, jbyteArray paramP,
    jbyteArray paramA, jbyteArray paramB)
{
    if (p != NULL) {
        BN_free(p);
    }
    if (a != NULL) {
        BN_free(a);
    }
    if (b != NULL) {
        BN_free(b);
    }
    if (paramP != NULL) {
        (*env)->DeleteLocalRef(env, paramP);
    }
    if (paramA != NULL) {
        (*env)->DeleteLocalRef(env, paramA);
    }
    if (paramB != NULL) {
        (*env)->DeleteLocalRef(env, paramB);
    }
}

// Set p, a, b in group to params.
static bool SetECDHCurve(JNIEnv* env, EC_GROUP* group, jobjectArray params)
{
    BIGNUM* p = NULL;
    BIGNUM* a = NULL;
    BIGNUM* b = NULL;
    jbyteArray paramP = NULL;
    jbyteArray paramA = NULL;
    jbyteArray paramB = NULL;
    if ((p = BN_new()) == NULL || (a = BN_new()) == NULL || (b = BN_new()) == NULL) {
        KAE_ThrowOOMException(env, "failed to allocate BN_new");
        goto cleanup;
    }
    if (!EC_GROUP_get_curve_GFp(group, p, a, b, NULL)) {
        goto cleanup;
    }

    // Set p.
    if ((paramP = KAE_GetByteArrayFromBigNum(env, p)) == NULL) {
        goto cleanup;
    }
    (*env)->SetObjectArrayElement(env, params, ecdhP, paramP);

    // Set a.
    if ((paramA = KAE_GetByteArrayFromBigNum(env, a)) == NULL) {
        goto cleanup;
    }
    (*env)->SetObjectArrayElement(env, params, ecdhA, paramA);

    // Set b.
    if ((paramB = KAE_GetByteArrayFromBigNum(env, b)) == NULL) {
        goto cleanup;
    }
    (*env)->SetObjectArrayElement(env, params, ecdhB, paramB);
    FreeECDHCurveParam(env, p, a, b, paramP, paramA, paramB);
    return true;

cleanup:
    FreeECDHCurveParam(env, p, a, b, paramP, paramA, paramB);
    return false;
}

// Set generator(x, y) in group to params.
static bool SetECDHPoint(JNIEnv* env, EC_GROUP* group, jobjectArray params)
{
    BIGNUM* x = NULL;
    BIGNUM* y = NULL;
    const EC_POINT* generator = NULL;
    jbyteArray paramX = NULL;
    jbyteArray paramY = NULL;
    if ((x = BN_new()) == NULL || (y = BN_new()) == NULL) {
        KAE_ThrowOOMException(env, "failed to allocate BN_new");
        goto cleanup;
    }
    if ((generator = EC_GROUP_get0_generator(group)) == NULL) {
        KAE_ThrowOOMException(env, "failed to allocate ec generator");
        goto cleanup;
    }
    if (!EC_POINT_get_affine_coordinates_GFp(group, generator, x, y, NULL)) {
        KAE_ThrowFromOpenssl(env, "EC_POINT_set_affine_coordinates_GFp", KAE_ThrowRuntimeException);
        goto cleanup;
    }

    // Set x.
    if ((paramX = KAE_GetByteArrayFromBigNum(env, x)) == NULL) {
        goto cleanup;
    }
    (*env)->SetObjectArrayElement(env, params, ecdhX, paramX);

    // Set y.
    if ((paramY = KAE_GetByteArrayFromBigNum(env, y)) == NULL) {
        goto cleanup;
    }
    (*env)->SetObjectArrayElement(env, params, ecdhY, paramY);
    BN_free(x);
    BN_free(y);
    (*env)->DeleteLocalRef(env, paramX);
    (*env)->DeleteLocalRef(env, paramY);
    return true;

cleanup:
    if (x != NULL) {
        BN_free(x);
    }
    if (y != NULL) {
        BN_free(y);
    }
    if (paramX != NULL) {
        (*env)->DeleteLocalRef(env, paramX);
    }
    if (paramY != NULL) {
        (*env)->DeleteLocalRef(env, paramY);
    }
    return false;
}

// Set order, cofactor in group to params.
static bool SetECDHOrderAndCofactor(JNIEnv* env, EC_GROUP* group, jobjectArray params)
{
    BIGNUM* order = NULL;
    BIGNUM* cofactor = NULL;
    jbyteArray paramOrder = NULL;
    jbyteArray paramCofactor = NULL;
    if ((order = BN_new()) == NULL || (cofactor = BN_new()) == NULL) {
        goto cleanup;
    }
    if (!EC_GROUP_get_order(group, order, NULL)) {
        goto cleanup;
    }

    // Set order.
    if ((paramOrder = KAE_GetByteArrayFromBigNum(env, order)) == NULL) {
        goto cleanup;
    }
    (*env)->SetObjectArrayElement(env, params, ecdhOrder, paramOrder);
    if (!EC_GROUP_get_cofactor(group, cofactor, NULL)) {
        goto cleanup;
    }

    // Set cofactor.
    if ((paramCofactor = KAE_GetByteArrayFromBigNum(env, cofactor)) == NULL) {
        goto cleanup;
    }
    (*env)->SetObjectArrayElement(env, params, ecdhCofactor, paramCofactor);
    BN_free(order);
    BN_free(cofactor);
    (*env)->DeleteLocalRef(env, paramOrder);
    (*env)->DeleteLocalRef(env, paramCofactor);
    return true;

cleanup:
    if (order != NULL) {
        BN_free(order);
    }
    if (cofactor != NULL) {
        BN_free(cofactor);
    }
    if (paramOrder != NULL) {
        (*env)->DeleteLocalRef(env, paramOrder);
    }
    if (paramCofactor != NULL) {
        (*env)->DeleteLocalRef(env, paramCofactor);
    }
    return false;
}

static void FreeECDHKeyParam(JNIEnv* env,
    BIGNUM* wX, BIGNUM* wY, jbyteArray keyWX, jbyteArray keyWY, jbyteArray keyS)
{
    if (wX != NULL) {
        BN_free(wX);
    }
    if (wY != NULL) {
        BN_free(wY);
    }
    if (keyWX != NULL) {
        (*env)->DeleteLocalRef(env, keyWX);
    }
    if (keyWY != NULL) {
        (*env)->DeleteLocalRef(env, keyWY);
    }
    if (keyS != NULL) {
        (*env)->DeleteLocalRef(env, keyS);
    }
}

// Set publicKey(wX, wY) and privateKey(s) in eckey to params.
static bool SetECDHKey(JNIEnv* env, const EC_GROUP* group, jobjectArray params,
    const EC_KEY* eckey)
{
    BIGNUM* wX = NULL;
    BIGNUM* wY = NULL;
    const EC_POINT* pub = NULL;
    const BIGNUM* s = NULL;
    jbyteArray keyWX = NULL;
    jbyteArray keyWY = NULL;
    jbyteArray keyS = NULL;
    if ((wX = BN_new()) == NULL || (wY = BN_new()) == NULL) {
        KAE_ThrowOOMException(env, "failed to allocate array");
        goto cleanup;
    }

    if ((pub = EC_KEY_get0_public_key(eckey)) == NULL ||
        !EC_POINT_get_affine_coordinates_GFp(group, pub, wX, wY, NULL)) {
        goto cleanup;
    }
    if ((s = EC_KEY_get0_private_key(eckey)) == NULL) {
        goto cleanup;
    }

    // Set wX.
    if ((keyWX = KAE_GetByteArrayFromBigNum(env, wX)) == NULL) {
        goto cleanup;
    }
    (*env)->SetObjectArrayElement(env, params, ecdhWX, keyWX);

    // Set wY.
    if ((keyWY = KAE_GetByteArrayFromBigNum(env, wY)) == NULL) {
        goto cleanup;
    }
    (*env)->SetObjectArrayElement(env, params, ecdhWY, keyWY);

    // Set s.
    if ((keyS = KAE_GetByteArrayFromBigNum(env, s)) == NULL) {
        goto cleanup;
    }
    (*env)->SetObjectArrayElement(env, params, ecdhS, keyS);
    FreeECDHKeyParam(env, wX, wY, keyWX, keyWY, keyS);
    return true;

cleanup:
    FreeECDHKeyParam(env, wX, wY, keyWX, keyWY, keyS);
    return false;
}

// Convert EC_GROUP in openssl to byte[][] in java
static jobjectArray NewECDHParam(JNIEnv* env, EC_GROUP* group)
{
    jclass byteArrayClass = (*env)->FindClass(env, "[B");
    jobjectArray params = (*env)->NewObjectArray(env, KAE_EC_PARAM_NUM_SIZE, byteArrayClass, NULL);
    if (params == NULL) {
        KAE_ThrowOOMException(env, "failed to allocate array");
        goto cleanup;
    }

    if (!SetECDHCurve(env, group, params)) {
        goto cleanup;
    }
    if (!SetECDHPoint(env, group, params)) {
        goto cleanup;
    }
    if (!SetECDHOrderAndCofactor(env, group, params)) {
        goto cleanup;
    }

    (*env)->DeleteLocalRef(env, byteArrayClass);
    return params;

cleanup:
    if (byteArrayClass != NULL) {
        (*env)->DeleteLocalRef(env, byteArrayClass);
    }
    if (params != NULL) {
        (*env)->DeleteLocalRef(env, params);
    }
    return NULL;
}

// Convert EC_KEY in openssl to byte[][] in java
static jobjectArray NewECDHKey(JNIEnv* env, const EC_GROUP* group, const EC_KEY* eckey)
{
    jclass byteArrayClass = (*env)->FindClass(env, "[B");
    jobjectArray params = (*env)->NewObjectArray(env, KAE_EC_KEY_NUM_SIZE, byteArrayClass, NULL);
    if (params == NULL) {
        KAE_ThrowOOMException(env, "failed to allocate array");
        goto cleanup;
    }
    if (!SetECDHKey(env, group, params, eckey)) {
        goto cleanup;
    }

    (*env)->DeleteLocalRef(env, byteArrayClass);
    return params;

cleanup:
    if (byteArrayClass != NULL) {
        (*env)->DeleteLocalRef(env, byteArrayClass);
    }
    if (params != NULL) {
        (*env)->DeleteLocalRef(env, params);
    }
    return NULL;
}

static void FreeECDHParam(BIGNUM* p, BIGNUM* a, BIGNUM* b, BIGNUM* x, BIGNUM* y, BIGNUM* order, BIGNUM* cofactor)
{
    KAE_ReleaseBigNumFromByteArray(p);
    KAE_ReleaseBigNumFromByteArray(a);
    KAE_ReleaseBigNumFromByteArray(b);
    KAE_ReleaseBigNumFromByteArray(x);
    KAE_ReleaseBigNumFromByteArray(y);
    KAE_ReleaseBigNumFromByteArray(order);
    KAE_ReleaseBigNumFromByteArray(cofactor);
}

// Convert params in java to EC_GROUP in openssl
static EC_GROUP* GetGroupByParam(JNIEnv* env, jbyteArray pArr, jbyteArray aArr, jbyteArray bArr,
    jbyteArray xArr, jbyteArray yArr, jbyteArray orderArr, jint cofactorInt)
{
    BIGNUM* p = NULL;
    BIGNUM* a = NULL;
    BIGNUM* b = NULL;
    BIGNUM* x = NULL;
    BIGNUM* y = NULL;
    BIGNUM* order = NULL;
    BIGNUM* cofactor = NULL;
    EC_GROUP* group = NULL;
    BN_CTX* ctx = NULL;
    EC_POINT* generator = NULL;
    if ((p = KAE_GetBigNumFromByteArray(env, pArr)) == NULL || (a = KAE_GetBigNumFromByteArray(env, aArr)) == NULL ||
        (b = KAE_GetBigNumFromByteArray(env, bArr)) == NULL || (x = KAE_GetBigNumFromByteArray(env, xArr)) == NULL ||
        (y = KAE_GetBigNumFromByteArray(env, yArr)) == NULL || (cofactor = BN_new()) == NULL ||
        (order = KAE_GetBigNumFromByteArray(env, orderArr)) == NULL || !BN_set_word(cofactor, cofactorInt)) {
        goto cleanup;
    }

    // Create the curve.
    if ((ctx = BN_CTX_new()) == NULL || (group = EC_GROUP_new_curve_GFp(p, a, b, ctx)) == NULL) {
        goto cleanup;
    }

    // Create the generator and set x, y.
    if ((generator = EC_POINT_new(group)) == NULL ||
            !EC_POINT_set_affine_coordinates_GFp(group, generator, x, y, ctx)) {
        goto cleanup;
    }

    // Set the generator, order and cofactor.
    if (!EC_GROUP_set_generator(group, generator, order, cofactor)) {
        goto cleanup;
    }

    FreeECDHParam(p, a, b, x, y, order, cofactor);
    EC_POINT_free(generator);
    BN_CTX_free(ctx);
    return group;

cleanup:
    FreeECDHParam(p, a, b, x, y, order, cofactor);
    if (group != NULL) {
        EC_GROUP_free(group);
    }
    if (generator != NULL) {
        EC_POINT_free(generator);
    }
    if (ctx != NULL) {
        BN_CTX_free(ctx);
    }
    return NULL;
}


/*
 * Class:     org_openeuler_security_openssl_KAEECKeyPairGenerator
 * Method:    nativeGenerateParam
 * Signature: (Ljava/lang/String;)[[B
 */
JNIEXPORT jobjectArray JNICALL Java_org_openeuler_security_openssl_KAEECKeyPairGenerator_nativeGenerateParam(
    JNIEnv* env, jclass cls, jstring curveName)
{
    EC_GROUP* group = NULL;
    jobjectArray ecdhParam = NULL;

    const char *curve = (*env)->GetStringUTFChars(env, curveName, 0);
    KAE_TRACE("KAEECKeyPairGenerator_nativeGenerateParam(curveName = %s)", curve);
    int nid = OBJ_sn2nid(curve);
    (*env)->ReleaseStringUTFChars(env, curveName, curve);
    if (nid == NID_undef) {
        goto cleanup;
    }
    // Construct a builtin curve.
    if ((group = EC_GROUP_new_by_curve_name(nid)) == NULL) {
        goto cleanup;
    }
    ecdhParam = NewECDHParam(env, group);

    if (group != NULL) {
        EC_GROUP_free(group);
    }
    KAE_TRACE("KAEECKeyPairGenerator_nativeGenerateParam success, ecdhParam = %p", ecdhParam);
    return ecdhParam;

cleanup:
    if (group != NULL) {
        EC_GROUP_free(group);
    }
    if (ecdhParam != NULL) {
        (*env)->DeleteLocalRef(env, ecdhParam);
    }
    return NULL;
}

/*
 * Class:     org_openeuler_security_openssl_KAEECKeyPairGenerator
 * Method:    nativeGenerateKeyPair
 * Signature: ([B[B[B[B[B[BI)[[B
 */
JNIEXPORT jobjectArray JNICALL Java_org_openeuler_security_openssl_KAEECKeyPairGenerator_nativeGenerateKeyPair(
    JNIEnv* env, jclass cls, jbyteArray pArr, jbyteArray aArr, jbyteArray bArr,
    jbyteArray xArr, jbyteArray yArr, jbyteArray orderArr, jint cofactorInt)
{
    EC_GROUP* group = NULL;
    EC_KEY* eckey = NULL;
    jobjectArray ecdhKey = NULL;

    if ((group = GetGroupByParam(env, pArr, aArr, bArr, xArr, yArr, orderArr, cofactorInt)) == NULL) {
        goto cleanup;
    }
    if ((eckey = EC_KEY_new()) == NULL) {
        goto cleanup;
    }
    if (!EC_KEY_set_group(eckey, group)) {
        goto cleanup;
    }
    // Generates a new public and private key for the supplied eckey object.
    // Refer to {@link https://www.openssl.org/docs/man1.1.0/man3/EC_KEY_generate_key.html} for details.
    if (!EC_KEY_generate_key(eckey)) {
        goto cleanup;
    }

    ecdhKey = NewECDHKey(env, group, eckey);

    EC_KEY_free(eckey);
    EC_GROUP_free(group);

    KAE_TRACE("KAEECKeyPairGenerator_nativeGenerateKeyPair success, ecdhKey = %p", ecdhKey);
    return ecdhKey;

cleanup:
    if (eckey != NULL) {
        EC_KEY_free(eckey);
    }
    if (group != NULL) {
        EC_GROUP_free(group);
    }
    if (ecdhKey != NULL) {
        (*env)->DeleteLocalRef(env, ecdhKey);
    }
    return NULL;
}
