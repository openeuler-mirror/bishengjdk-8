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

import javax.crypto.KeyAgreement;
import java.security.PublicKey;
import java.security.PrivateKey;
import java.security.KeyPair;
import java.security.KeyPairGenerator;

@Warmup(iterations = 10, time = 2, timeUnit = TimeUnit.SECONDS)
public class ECKeyAgreementBenchmark extends BenchmarkBase {
    @Param({"EC"})
    private String kpgAlgorithm;

    @Param({"ECDH"})
    private String algorithm;

    @Param({"224", "256", "384", "521"})
    private int keySize;

    private KeyPairGenerator keyPairGenerator;
    private KeyAgreement keyAgreement;
    private PrivateKey privKey;
    private PublicKey pubKey;

    @Setup
    public void setUp() throws Exception {
        setupProvider();
        keyPairGenerator = createKeyPairGenerator();
        keyPairGenerator.initialize(keySize);
        KeyPair kpA = keyPairGenerator.generateKeyPair();
        privKey = kpA.getPrivate();
        KeyPair kpB = keyPairGenerator.generateKeyPair();
        pubKey = kpB.getPublic();
        keyPairGenerator = createKeyPairGenerator();
        keyAgreement = createKeyAgreement();
    }

    @Benchmark
    public void generateKeyAgreement() throws Exception {
        keyAgreement.init(privKey);
        keyAgreement.doPhase(pubKey, true);
        keyAgreement.generateSecret();
    }

    private KeyPairGenerator createKeyPairGenerator() throws Exception {
        if (prov != null) {
            return KeyPairGenerator.getInstance(kpgAlgorithm, prov);
        }
        return KeyPairGenerator.getInstance(kpgAlgorithm);
    }

    private KeyAgreement createKeyAgreement() throws Exception {
        if (prov != null) {
            return KeyAgreement.getInstance(algorithm, prov);
        }
        return KeyAgreement.getInstance(algorithm);
    }
}
