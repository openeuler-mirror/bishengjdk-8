/*
 * Copyright (c) 2019, Huawei Technologies Co. Ltd. All rights reserved.
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
package com.huawei.jvm.gc;
import com.huawei.management.AdaptiveHeapMXBean;
import sun.management.Util;
import javax.management.ObjectName;

public class AdaptiveHeapMXBeanImpl implements AdaptiveHeapMXBean {
    private static native void registerNatives();

    static {
        registerNatives();
    }

    private final static String ADAPTIVE_HEAP_MXBEAN_NAME = "com.huawei.management:type=AdaptiveHeap";
    @Override
    public void setG1PeriodicGCInterval(int interval) {
        setG1PeriodicGCIntervalImpl(interval);
    }
    @Override
    public void setG1PeriodicGCLoadThreshold(int loadThreshold) {
        setG1PeriodicGCLoadThresholdImpl(loadThreshold);
    }
    @Override
    public int getG1PeriodicGCInterval() {
        return getG1PeriodicGCIntervalImpl();
    }
    @Override
    public int getG1PeriodicGCLoadThreshold() {
        return getG1PeriodicGCLoadThresholdImpl();
    }
    @Override
    public ObjectName getObjectName() {
        return Util.newObjectName(ADAPTIVE_HEAP_MXBEAN_NAME);
    }


    private static native void setG1PeriodicGCIntervalImpl(int interval);
    private static native void setG1PeriodicGCLoadThresholdImpl(int loadThreshold);
    private static native int getG1PeriodicGCIntervalImpl();
    private static native int getG1PeriodicGCLoadThresholdImpl();
}
