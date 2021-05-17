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

#ifndef KAE_EXCEPTION_H
#define KAE_EXCEPTION_H

#include <jni.h>

/* Throw a Java exception by name */
void KAE_ThrowByName(JNIEnv* env, const char* name, const char* msg);

void KAE_ThrowOOMException(JNIEnv* env, const char* msg);

void KAE_ThrowNullPointerException(JNIEnv* env, const char* msg);

void KAE_ThrowArrayIndexOutOfBoundsException(JNIEnv* env, const char* msg);

void KAE_ThrowFromOpenssl(JNIEnv* env, const char* msg, void (* defaultException)(JNIEnv*, const char*));

void KAE_ThrowEvpException(JNIEnv* env, int reason, const char* msg, void (* defaultException)(JNIEnv*, const char*));

void KAE_ThrowRuntimeException(JNIEnv* env, const char* msg);

void KAE_ThrowBadPaddingException(JNIEnv* env, const char* msg);

/* Throw InvalidKeyException */
void KAE_ThrowInvalidKeyException(JNIEnv* env, const char* msg);

/* Throw AlgorithmParameterException */
void KAE_ThrowInvalidAlgorithmParameterException(JNIEnv* env, const char* msg);

void KAE_ThrowAEADBadTagException(JNIEnv* env, const char* msg);

void KAE_ThrowSignatureException(JNIEnv* env, const char* msg);

void KAE_ThrowClassNotFoundException(JNIEnv* env, const char* msg);
#endif
