/*
 * Copyright (c) 2021, Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2015, 2018, Oracle and/or its affiliates. All rights reserved.
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

import java.security.*;
import java.security.spec.MGF1ParameterSpec;
import java.security.spec.PSSParameterSpec;
import java.util.HashMap;
import java.util.Map;

/**
 * RSASSA-PSS Signature Benchmark
 */
public class RSAPSSSignatureBenchmark extends BenchmarkBase {
    private static Map<String, PSSParameterSpec> pssParameterSpecMap;

    static {
        initPSSParameterSpecMap();
    }

    @Param({"SHA-1", "SHA-224", "SHA-256", "SHA-384", "SHA-512"})
    private String algorithm;

    @Param({"2048", "3072", "4096"})
    private int keySize;

    @Param({"" + 1024, "" + 10 * 1024, "" + 100 * 1024, "" + 256 * 1024, "" + 1024 * 1024, "" + 10 * 1024 * 1024})
    private int dataSize;

    private KeyPair keyPair;

    private byte[][] sigData;


    private static void initPSSParameterSpecMap() {
        pssParameterSpecMap = new HashMap<>();
        pssParameterSpecMap.put("SHA-1", new PSSParameterSpec("SHA-1",
                "MGF1", MGF1ParameterSpec.SHA1, 20, PSSParameterSpec.TRAILER_FIELD_BC));
        pssParameterSpecMap.put("SHA-224", new PSSParameterSpec("SHA-224",
                "MGF1", MGF1ParameterSpec.SHA224, 28, PSSParameterSpec.TRAILER_FIELD_BC));
        pssParameterSpecMap.put("SHA-256", new PSSParameterSpec("SHA-256",
                "MGF1", MGF1ParameterSpec.SHA256, 32, PSSParameterSpec.TRAILER_FIELD_BC));
        pssParameterSpecMap.put("SHA-384", new PSSParameterSpec("SHA-384",
                "MGF1", MGF1ParameterSpec.SHA384, 48, PSSParameterSpec.TRAILER_FIELD_BC));
        pssParameterSpecMap.put("SHA-512", new PSSParameterSpec("SHA-512",
                "MGF1", MGF1ParameterSpec.SHA512, 64, PSSParameterSpec.TRAILER_FIELD_BC));
    }

    @Setup
    public void setup() throws Exception {
        setupProvider();
        KeyPairGenerator keyPairGenerator = KeyPairGenerator.getInstance("RSA");
        keyPairGenerator.initialize(keySize);
        keyPair = keyPairGenerator.generateKeyPair();
        data = new byte[SET_SIZE][dataSize];
        sigData = getSigBytes(data);
    }

    private byte[][] getSigBytes(byte[][] data) throws Exception {
        byte[][] sigBytes = new byte[data.length][];
        Signature signature = prov != null ? Signature.getInstance("RSASSA-PSS", prov) :
                Signature.getInstance("RSASSA-PSS");
        signature.setParameter(pssParameterSpecMap.get(algorithm));
        signature.initSign(keyPair.getPrivate());
        for (int i = 0; i < sigBytes.length; i++) {
            signature.update(data[i]);
            sigBytes[i] = signature.sign();
        }
        return sigBytes;
    }

    @Benchmark
    public void sign() throws Exception {
        Signature signature = prov != null ? Signature.getInstance("RSASSA-PSS", prov) :
                Signature.getInstance("RSASSA-PSS");
        signature.setParameter(pssParameterSpecMap.get(algorithm));
        signature.initSign(keyPair.getPrivate());
        signature.update(data[index]);
        signature.sign();
        index = (index + 1) % SET_SIZE;
    }

    @Benchmark
    public void verify() throws Exception {
        Signature signature = prov != null ? Signature.getInstance("RSASSA-PSS", prov) :
                Signature.getInstance("RSASSA-PSS");
        signature.setParameter(pssParameterSpecMap.get(algorithm));
        signature.initVerify(keyPair.getPublic());
        signature.update(data[index]);
        signature.verify(sigData[index]);
        index = (index + 1) % SET_SIZE;
    }
}
