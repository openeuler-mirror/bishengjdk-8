/*
 * Copyright (c) 2019 Alibaba Group Holding Limited. All Rights Reserved.
 * Copyright (c) 2021, Huawei Technologies Co., Ltd. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation. Alibaba designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "jni.h"
#include "jvm.h"
#include "com_huawei_jvm_gc_AdaptiveHeapMXBeanImpl.h"

#define ARRAY_LENGTH(a) (sizeof(a)/sizeof(a[0]))

static JNINativeMethod methods[] = {
  {"setG1PeriodicGCIntervalImpl",        "(I)V", (void *)&JVM_AdaptiveHeapSetG1PeriodicGCInterval},
  {"getG1PeriodicGCIntervalImpl",        "()I",  (void *)&JVM_AdaptiveHeapGetG1PeriodicGCInterval},
  {"setG1PeriodicGCLoadThresholdImpl",   "(I)V", (void *)&JVM_AdaptiveHeapSetG1PeriodicGCLoadThreshold},
  {"getG1PeriodicGCLoadThresholdImpl",   "()I",  (void *)&JVM_AdaptiveHeapGetG1PeriodicGCLoadThreshold},

};

JNIEXPORT void JNICALL
Java_com_huawei_jvm_gc_AdaptiveHeapMXBeanImpl_registerNatives(JNIEnv *env, jclass cls)
{
  (*env)->RegisterNatives(env, cls, methods, ARRAY_LENGTH(methods));
}
