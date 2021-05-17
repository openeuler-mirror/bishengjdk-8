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
#include "kae_util.h"
#include "kae_log.h"
#include "kae_exception.h"

void KAE_ThrowByName(JNIEnv* env, const char* name, const char* msg) {
    jclass cls = (*env)->FindClass(env, name);
    if (cls != 0) {
        (*env)->ThrowNew(env, cls, msg);
        (*env)->DeleteLocalRef(env, cls);
    }
}

void KAE_ThrowOOMException(JNIEnv* env, const char* msg) {
    KAE_ThrowByName(env, "java/lang/OutOfMemoryError", msg);
}

void KAE_ThrowNullPointerException(JNIEnv* env, const char* msg) {
    KAE_ThrowByName(env, "java/lang/NullPointerException", msg);
}

void KAE_ThrowArrayIndexOutOfBoundsException(JNIEnv* env, const char* msg) {
    KAE_ThrowByName(env, "java/lang/ArrayIndexOutOfBoundsException", msg);
}

void KAE_ThrowEvpException(JNIEnv* env, int reason, const char* msg, void (* defaultException)(JNIEnv*, const char*)) {
    switch (reason) {
        case EVP_R_UNSUPPORTED_ALGORITHM:
            KAE_ThrowByName(env, "java/security/NoSuchAlgorithmException", msg);
            break;
        case EVP_R_MISSING_PARAMETERS:
            KAE_ThrowByName(env, "java/security/InvalidKeyException", msg);
            break;
        case EVP_R_BAD_DECRYPT:
        case EVP_R_DATA_NOT_MULTIPLE_OF_BLOCK_LENGTH:
            KAE_ThrowByName(env, "javax/crypto/BadPaddingException", msg);
            break;
        default:
            defaultException(env, msg);
            break;
    }
}

void KAE_ThrowRuntimeException(JNIEnv* env, const char* msg) {
    KAE_ThrowByName(env, "java/lang/RuntimeException", msg);
}

void KAE_ThrowBadPaddingException(JNIEnv* env, const char* msg) {
    KAE_ThrowByName(env, "javax/crypto/BadPaddingException", msg);
}

void KAE_ThrowInvalidKeyException(JNIEnv* env, const char* msg) {
    KAE_ThrowByName(env, "java/security/InvalidKeyException", msg);
}

void KAE_ThrowInvalidAlgorithmParameterException(JNIEnv* env, const char* msg) {
    KAE_ThrowByName(env, "java/security/InvalidAlgorithmParameterException", msg);
}

void KAE_ThrowFromOpenssl(JNIEnv* env, const char* msg, void (* defaultException)(JNIEnv*, const char*)) {
    const char* file = NULL;
    const char* data = NULL;
    int line = 0;
    int flags = 0;
    unsigned long err;
    static const int ESTRING_SIZE = 256;

    err = ERR_get_error_line_data(&file, &line, &data, &flags);
    if (err == 0) {
        defaultException(env, msg);
        return;
    }

    if (!(*env)->ExceptionCheck(env)) {
        char estring[ESTRING_SIZE];
        ERR_error_string_n(err, estring, ESTRING_SIZE);
        int lib = ERR_GET_LIB(err);
        int reason = ERR_GET_REASON(err);
        KAE_TRACE("OpenSSL error in %s: err=%lx, lib=%x, reason=%x, file=%s, line=%d, estring=%s, data=%s", msg, err,
                  lib, reason, file, line, estring, (flags & ERR_TXT_STRING) ? data : "(no data)");

        switch (lib) {
            case ERR_LIB_EVP:
            case ERR_LIB_RSA:
                KAE_ThrowEvpException(env, reason, estring, defaultException);
                break;
            default:
                defaultException(env, estring);
                break;
        }
    }

    ERR_clear_error();
}

void KAE_ThrowAEADBadTagException(JNIEnv *env, const char *msg) {
    KAE_ThrowByName(env, "javax/crypto/AEADBadTagException", msg);
}

void KAE_ThrowSignatureException(JNIEnv* env, const char* msg) {
    KAE_ThrowByName(env, "java/security/SignatureException", msg);
}

void KAE_ThrowClassNotFoundException(JNIEnv* env, const char* msg) {
    KAE_ThrowByName(env, "java/lang/ClassNotFoundException", msg);
}