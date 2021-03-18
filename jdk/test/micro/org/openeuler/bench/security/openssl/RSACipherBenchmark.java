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

import javax.crypto.BadPaddingException;
import javax.crypto.Cipher;
import javax.crypto.IllegalBlockSizeException;
import javax.crypto.NoSuchPaddingException;
import java.security.InvalidKeyException;
import java.security.KeyPair;
import java.security.KeyPairGenerator;
import java.security.NoSuchAlgorithmException;

public class RSACipherBenchmark extends BenchmarkBase {
    @Param({"RSA/ECB/NoPadding", "RSA/ECB/PKCS1Padding", "RSA/ECB/OAEPPadding"})
    private String algorithm;

    @Param({"512", "1024", "2048", "3072", "4096"})
    private int keyLength;

    @Param({"true", "false"})
    private boolean encryptPublicKey;

    private byte[][] data;
    private byte[][] encryptedData;

    private Cipher encryptCipher;
    private Cipher decryptCipher;
    private int index = 0;

    private int getMaxDataSize(int keyLength, String algorithm) {
        int dataSize = keyLength / 8;
        if ("RSA/ECB/PKCS1Padding".equals(algorithm)) {
            return dataSize - 11;
        }

        if ("RSA/ECB/OAEPPadding".equals(algorithm)) {
            // SHA-1 digestLen is 20
            int digestLen = 20;
            return dataSize - 2 - 2 * digestLen;
        }
        return dataSize;
    }

    @Setup()
    public void setup() throws NoSuchAlgorithmException, NoSuchPaddingException, InvalidKeyException, BadPaddingException, IllegalBlockSizeException {
        setupProvider();

        int dataSize = getMaxDataSize(keyLength, algorithm);
        data = fillRandom(new byte[SET_SIZE][dataSize - 1]);

        KeyPairGenerator kpg = KeyPairGenerator.getInstance("RSA");
        kpg.initialize(keyLength);
        KeyPair keyPair = kpg.generateKeyPair();

        encryptCipher = (prov == null) ? Cipher.getInstance(algorithm) : Cipher.getInstance(algorithm, prov);
        decryptCipher = (prov == null) ? Cipher.getInstance(algorithm) : Cipher.getInstance(algorithm, prov);
        if (encryptPublicKey || "RSA/ECB/OAEPPadding".equals(algorithm)) {
            encryptCipher.init(Cipher.ENCRYPT_MODE, keyPair.getPublic());
            decryptCipher.init(Cipher.DECRYPT_MODE, keyPair.getPrivate());
        } else {
            encryptCipher.init(Cipher.ENCRYPT_MODE, keyPair.getPrivate());
            decryptCipher.init(Cipher.DECRYPT_MODE, keyPair.getPublic());
        }
        encryptedData = fillEncrypted(data, encryptCipher);
    }

    @Benchmark
    public byte[] encrypt() throws BadPaddingException, IllegalBlockSizeException {
        byte[] dataBytes = data[index];
        index = (index + 1) % data.length;
        return encryptCipher.doFinal(dataBytes);
    }

    @Benchmark
    public byte[] decrypt() throws BadPaddingException, IllegalBlockSizeException {
        byte[] e = encryptedData[index];
        index = (index + 1) % encryptedData.length;
        return decryptCipher.doFinal(e);
    }
}
