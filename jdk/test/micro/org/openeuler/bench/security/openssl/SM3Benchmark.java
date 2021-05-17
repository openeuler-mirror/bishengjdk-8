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

import org.openeuler.security.openssl.KAEProvider;
import org.openjdk.jmh.annotations.BenchmarkMode;
import org.openjdk.jmh.annotations.Benchmark;
import org.openjdk.jmh.annotations.Fork;
import org.openjdk.jmh.annotations.Measurement;
import org.openjdk.jmh.annotations.Mode;
import org.openjdk.jmh.annotations.OutputTimeUnit;
import org.openjdk.jmh.annotations.Scope;
import org.openjdk.jmh.annotations.Param;
import org.openjdk.jmh.annotations.Setup;
import org.openjdk.jmh.annotations.State;
import org.openjdk.jmh.annotations.Threads;
import org.openjdk.jmh.annotations.Warmup;

import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.security.Provider;
import java.security.Security;
import java.util.Random;
import java.util.concurrent.TimeUnit;

@BenchmarkMode(Mode.Throughput)
@OutputTimeUnit(TimeUnit.SECONDS)
@Warmup(iterations = 3, time = 3, timeUnit = TimeUnit.SECONDS)
@Measurement(iterations = 8, time = 2, timeUnit = TimeUnit.SECONDS)
@Fork(jvmArgsPrepend = {"-Xms100G", "-Xmx100G", "-XX:+AlwaysPreTouch"}, value = 5)
@Threads(1)
@State(Scope.Thread)
public class SM3Benchmark {
    public static final int SET_SIZE = 128;
    byte[][] data;
    int index = 0;

    @Param({"SM3"})
    private String algorithm;

    @Param({"" + 1024, "" + 10 * 1024, "" + 100 * 1024, "" + 1024 * 1024})
    int dataSize;

    MessageDigest md;

    @Setup
    public void setup() throws NoSuchAlgorithmException {
        Security.addProvider(new KAEProvider());
        Provider prov = Security.getProvider("KAEProvider");
        data = fillRandom(new byte[SET_SIZE][dataSize]);
        md = (prov == null) ? MessageDigest.getInstance(algorithm) : MessageDigest.getInstance(algorithm, prov);
    }

    @Benchmark
    public byte[] digest() {
        byte[] d = data[index];
        index = (index + 1) % SET_SIZE;
        return md.digest(d);
    }

    @Benchmark
    @Fork(jvmArgsPrepend = {"-Xms100G", "-Xmx100G", "-XX:+AlwaysPreTouch", "-Dkae.disableKaeDispose=true"}, value = 5)
    public byte[] digestDispose() {
        byte[] d = data[index];
        index = (index + 1) % SET_SIZE;
        return md.digest(d);
    }

    public static byte[][] fillRandom(byte[][] data) {
        Random rnd = new Random();
        for (byte[] d : data) {
            rnd.nextBytes(d);
        }
        return data;
    }
}

