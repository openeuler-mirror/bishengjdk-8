/*
 * Copyright (c) 2025, Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2019 Alibaba Group Holding Limited. All Rights Reserved.
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
 *
 */

 package com.huawei.jprofilecache;
 
 public class JProfileCache {
 
     // register native methods
     private static native void registerNatives();
 
     static {
         registerNatives();
     }
 
     private static boolean isStartupSignaled = false;
 
     public static synchronized void triggerPrecompilation() {
         if (!isStartupSignaled) {
             isStartupSignaled = true;
             triggerPrecompilation0();
         } else {
             throw new IllegalStateException("triggerPrecompilation can be triggered only once");
         }
     }
 
     public static synchronized void notifyJVMDeoptProfileCacheMethods() {
         if (isStartupSignaled && checkIfCompilationIsComplete()) {
             notifyJVMDeoptProfileCacheMethods0();
         }
     }
 
     public static synchronized boolean checkIfCompilationIsComplete() {
         if (!isStartupSignaled) {
           throw new IllegalStateException("Must call checkIfCompilationIsComplete after triggerPrecompilation");
         } else {
           return checkIfCompilationIsComplete0();
         }
     }

     // use for internal validation
     private void dummy() {
         throw new UnsupportedOperationException("dummy function");
     }
 
     private native static void triggerPrecompilation0();
 
     private native static boolean checkIfCompilationIsComplete0();
 
     private native static void notifyJVMDeoptProfileCacheMethods0();
 }