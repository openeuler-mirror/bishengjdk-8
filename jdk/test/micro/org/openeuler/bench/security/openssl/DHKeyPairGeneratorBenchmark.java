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
import org.openjdk.jmh.annotations.Param;
import org.openjdk.jmh.annotations.Setup;
import java.util.concurrent.TimeUnit;
import org.openjdk.jmh.annotations.Warmup;

import java.security.KeyPairGenerator;
import java.security.KeyPair;

public class DHKeyPairGeneratorBenchmark extends BenchmarkBase {
    @Param({"DH"})
    private String algorithm;

    @Param({"512", "1024", "2048", "3072", "4096"})
    private int keyLength;

    private KeyPairGenerator keyPairGenerator;

    @Setup
    public void setUp() throws Exception {
        setupProvider();
        keyPairGenerator = createKeyPairGenerator();
    }

    @Benchmark
    public KeyPair generateKeyPair() throws Exception {
        keyPairGenerator.initialize(keyLength);
        return keyPairGenerator.generateKeyPair();
    }

    private KeyPairGenerator createKeyPairGenerator() throws Exception {
        if (prov != null) {
            return KeyPairGenerator.getInstance(algorithm, prov);
        }
        return KeyPairGenerator.getInstance(algorithm);
    }
}

