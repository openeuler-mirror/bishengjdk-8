/*
 * Copyright (c) 2015, 2018, Oracle and/or its affiliates. All rights reserved.
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
package org.openeuler.bench.security.openssl;

import org.openjdk.jmh.annotations.Benchmark;
import org.openjdk.jmh.annotations.Fork;
import org.openjdk.jmh.annotations.Param;
import org.openjdk.jmh.annotations.Setup;

import java.security.InvalidKeyException;
import java.security.NoSuchAlgorithmException;
import java.security.Provider;
import java.security.Security;
import java.util.concurrent.TimeUnit;

import javax.crypto.KeyGenerator;
import javax.crypto.Mac;

public class HMacBenchmark extends BenchmarkBase {

    @Param({"HmacMD5", "HmacSHA1", "HmacSHA224", "HmacSHA256", "HmacSHA384", "HmacSHA512"})
    private String algorithm;

    @Param({"" + 1024, "" + 10 * 1024, "" + 100 * 1024, "" + 1024 * 1024})
    private int dataSize;

    private Mac mac;

    @Setup
    public void setup() throws NoSuchAlgorithmException, InvalidKeyException {
        setupProvider();
        mac = (prov == null) ? Mac.getInstance(algorithm) : Mac.getInstance(algorithm, prov);
        mac.init(KeyGenerator.getInstance(algorithm).generateKey());
        data = fillRandom(new byte[SET_SIZE][dataSize]);
    }

    @Benchmark
    public byte[] mac() {
        byte[] d = data[index];
        index = (index + 1) % SET_SIZE;
        return mac.doFinal(d);
    }

    @Benchmark
    @Fork(jvmArgsPrepend = {"-Xms100G", "-Xmx100G", "-XX:+AlwaysPreTouch", "-Dkae.disableKaeDispose=true"}, value = 5)
    public byte[] macDispose() {
        byte[] d = data[index];
        index = (index + 1) % SET_SIZE;
        return mac.doFinal(d);
    }
}

