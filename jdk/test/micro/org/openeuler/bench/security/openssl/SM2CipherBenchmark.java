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
import java.util.Random;
import java.util.concurrent.TimeUnit;

import javax.crypto.BadPaddingException;
import javax.crypto.Cipher;
import javax.crypto.IllegalBlockSizeException;
import javax.crypto.NoSuchPaddingException;

/**
 * SM2 Cipher Benchmark
 */
@BenchmarkMode(Mode.Throughput)
@OutputTimeUnit(TimeUnit.SECONDS)
@Warmup(iterations = 3, time = 3, timeUnit = TimeUnit.SECONDS)
@Measurement(iterations = 8, time = 2, timeUnit = TimeUnit.SECONDS)
@Fork(jvmArgsPrepend = {"-Xms100G", "-Xmx100G", "-XX:+AlwaysPreTouch"}, value = 5)
@Threads(1)
@State(Scope.Thread)
public class SM2CipherBenchmark {
    public static final int SET_SIZE = 128;
    byte[][] data;
    int index = 0;

    @Param({"SM2"})
    private String algorithm;

    @Param({"" + 1024, "" + 10 * 1024, "" + 100 * 1024, "" + 1024 * 1024})
    private int dataSize;

    @Param({"KAEProvider"})
    private String provider;

    public Provider prov = null;

    private KeyPair keyPair;

    private byte[][] encryptedData;
    private Cipher encryptCipher;
    private Cipher decryptCipher;

    @Setup
    public void setup() throws NoSuchAlgorithmException, NoSuchPaddingException, InvalidKeyException,
            IllegalBlockSizeException, BadPaddingException {
        Security.addProvider(new KAEProvider());
        prov = Security.getProvider(provider);

        KeyPairGenerator keyPairGenerator = KeyPairGenerator.getInstance("SM2");
        keyPair = keyPairGenerator.generateKeyPair();

        encryptCipher = (prov == null) ? Cipher.getInstance(algorithm) : Cipher.getInstance(algorithm, prov);
        encryptCipher.init(Cipher.ENCRYPT_MODE, keyPair.getPublic());
        decryptCipher = (prov == null) ? Cipher.getInstance(algorithm) : Cipher.getInstance(algorithm, prov);
        decryptCipher.init(Cipher.DECRYPT_MODE, keyPair.getPrivate());

        data = fillRandom(new byte[SET_SIZE][dataSize]);
        encryptedData = fillEncrypted(data, encryptCipher);
    }

    @Benchmark
    public byte[] encrypt() throws IllegalBlockSizeException, BadPaddingException {
        byte[] d = data[index];
        index = (index + 1) % SET_SIZE;
        return encryptCipher.doFinal(d);
    }

    @Benchmark
    public byte[] decrypt() throws IllegalBlockSizeException, BadPaddingException {
        byte[] e = encryptedData[index];
        index = (index + 1) % SET_SIZE;
        return decryptCipher.doFinal(e);
    }
    public static byte[][] fillRandom(byte[][] data) {
        Random rnd = new Random();
        for (byte[] d : data) {
            rnd.nextBytes(d);
        }
        return data;
    }

    public static byte[][] fillEncrypted(byte[][] data, Cipher encryptCipher)
            throws IllegalBlockSizeException, BadPaddingException {
        byte[][] encryptedData = new byte[data.length][];
        for (int i = 0; i < encryptedData.length; i++) {
            encryptedData[i] = encryptCipher.doFinal(data[i]);
        }
        return encryptedData;
    }
}

