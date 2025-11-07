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
package org.openeuler.bench.security.openssl;

import org.openeuler.security.openssl.KAEProvider;
import org.openjdk.jmh.annotations.*;

import java.security.*;
import java.util.concurrent.TimeUnit;

/**
 * SM2 Signature Benchmark
 */
@BenchmarkMode(Mode.Throughput)
@OutputTimeUnit(TimeUnit.SECONDS)
@Warmup(iterations = 3, time = 3, timeUnit = TimeUnit.SECONDS)
@Measurement(iterations = 8, time = 2, timeUnit = TimeUnit.SECONDS)
@Fork(jvmArgsPrepend = {"-Xms100G", "-Xmx100G", "-XX:+AlwaysPreTouch"}, value = 5)
@Threads(1)
@State(Scope.Thread)
public class SM2SignatureBenchmark {
    public static final int SET_SIZE = 128;
    byte[][] data;
    int index = 0;

    @Param({"SM3withSM2"})
    private String algorithm;

    @Param({"" + 1024, "" + 10 * 1024, "" + 100 * 1024, "" + 256 * 1024, "" + 1024 * 1024, "" + 10 * 1024 * 1024})
    private int dataSize;

    @Param({"KAEProvider"})
    private String provider;

    public Provider prov = null;

    private KeyPair keyPair;

    private byte[][] sigData;

    @Setup
    public void setup() throws Exception {
        Security.addProvider(new KAEProvider());
        prov = Security.getProvider(provider);

        KeyPairGenerator keyPairGenerator = KeyPairGenerator.getInstance("SM2");
        keyPair = keyPairGenerator.generateKeyPair();

        data = new byte[SET_SIZE][dataSize];
        sigData = getSigBytes(data);
    }

    private byte[][] getSigBytes(byte[][] data) throws Exception {
        byte[][] sigBytes = new byte[data.length][];
        Signature signature = prov != null ? Signature.getInstance(algorithm, prov) :
                Signature.getInstance(algorithm);
        signature.initSign(keyPair.getPrivate());
        for (int i = 0; i < sigBytes.length; i++) {
            signature.update(data[i]);
            sigBytes[i] = signature.sign();
        }
        return sigBytes;
    }

    @Benchmark
    public void sign() throws NoSuchAlgorithmException, InvalidKeyException, SignatureException {
        Signature signature = prov != null ? Signature.getInstance(algorithm, prov) :
                Signature.getInstance(algorithm);
        signature.initSign(keyPair.getPrivate());
        signature.update(data[index]);
        signature.sign();
        index = (index + 1) % SET_SIZE;
    }

    @Benchmark
    public void verify() throws NoSuchAlgorithmException, InvalidKeyException, SignatureException {
        Signature signature = prov != null ? Signature.getInstance(algorithm, prov) :
                Signature.getInstance(algorithm);
        signature.initVerify(keyPair.getPublic());
        signature.update(data[index]);
        signature.verify(sigData[index]);
        index = (index + 1) % SET_SIZE;
    }
}
