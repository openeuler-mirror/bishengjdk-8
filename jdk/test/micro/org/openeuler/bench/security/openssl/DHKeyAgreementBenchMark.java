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

import java.math.BigInteger;
import java.security.*;
import javax.crypto.*;
import javax.crypto.spec.*;
import org.openjdk.jmh.annotations.Benchmark;
import org.openjdk.jmh.annotations.Param;
import org.openjdk.jmh.annotations.Setup;
import java.util.concurrent.TimeUnit;
import org.openjdk.jmh.annotations.Warmup;

import java.security.KeyPairGenerator;

public class DHKeyAgreementBenchMark extends BenchmarkBase {
    @Param({"DH"})
    private String algorithm;

    @Param({"512", "1024", "2048", "3072", "4096"})
    private int keySize;

    private KeyPairGenerator aliceKpairGen;
    private KeyPairGenerator bobKpairGen;
    private KeyPairGenerator carolKpairGen;

    private KeyPair aliceKpair;
    private KeyPair bobKpair;
    private KeyPair carolKpair;

    private DHParameterSpec dhSkipParamSpec;

    @Setup
    public void setUp() throws Exception {
        setupProvider();
        aliceKpairGen = createKeyPairGenerator();
        bobKpairGen = createKeyPairGenerator();
        carolKpairGen = createKeyPairGenerator();

        // Alice creates her own DH key pair
        aliceKpairGen.initialize(keySize);
        aliceKpair = aliceKpairGen.generateKeyPair();
        // Bob creates his own DH key pair
        bobKpairGen.initialize(keySize);
        bobKpair = bobKpairGen.generateKeyPair();
        // Carol creates her own DH key pair
        carolKpairGen.initialize(keySize);
        carolKpair = carolKpairGen.generateKeyPair();
    }

    @Benchmark
    public void KeyAgreement() throws Exception {

        // Alice initialize
        KeyAgreement aliceKeyAgree = (prov == null ? KeyAgreement.getInstance("DH") : KeyAgreement.getInstance("DH", prov));
        aliceKeyAgree.init(aliceKpair.getPrivate());
        // Bob initialize
        KeyAgreement bobKeyAgree =  (prov == null ? KeyAgreement.getInstance("DH") : KeyAgreement.getInstance("DH", prov));
        bobKeyAgree.init(bobKpair.getPrivate());
        // Carol initialize
        KeyAgreement carolKeyAgree =  (prov == null ? KeyAgreement.getInstance("DH") : KeyAgreement.getInstance("DH", prov));
        carolKeyAgree.init(carolKpair.getPrivate());
        // Alice uses Carol's public key
        Key ac = aliceKeyAgree.doPhase(carolKpair.getPublic(), false);
        // Bob uses Alice's public key
        Key ba = bobKeyAgree.doPhase(aliceKpair.getPublic(), false);
        // Carol uses Bob's public key
        Key cb = carolKeyAgree.doPhase(bobKpair.getPublic(), false);
        // Alice uses Carol's result from above
        aliceKeyAgree.doPhase(cb, true);
        // Bob uses Alice's result from above
        bobKeyAgree.doPhase(ac, true);
        // Carol uses Bob's result from above
        carolKeyAgree.doPhase(ba, true);

        // Alice, Bob and Carol compute their secrets
        byte[] aliceSharedSecret = aliceKeyAgree.generateSecret();
        int aliceLen = aliceSharedSecret.length;

        byte[] bobSharedSecret = bobKeyAgree.generateSecret();
        int bobLen = bobSharedSecret.length;

        byte[] carolSharedSecret = carolKeyAgree.generateSecret();
        int carolLen = carolSharedSecret.length;

        // Compare Alice and Bob
        if (aliceLen != bobLen) {
            throw new Exception("Alice and Bob have different lengths");
        }
        for (int i=0; i<aliceLen; i++) {
            if (aliceSharedSecret[i] != bobSharedSecret[i]) {
                throw new Exception("Alice and Bob differ");
            }
        }

        // Compare Bob and Carol
        if (bobLen != carolLen) {
            throw new Exception("Bob and Carol have different lengths");
        }
        for (int i=0; i<bobLen; i++) {
            if (bobSharedSecret[i] != carolSharedSecret[i]) {
                throw new Exception("Bob and Carol differ");
            }
        }
    }


    private KeyPairGenerator createKeyPairGenerator() throws Exception {
        if (prov != null) {
            return KeyPairGenerator.getInstance(algorithm, prov);
        }
        return KeyPairGenerator.getInstance(algorithm);
    }

}

