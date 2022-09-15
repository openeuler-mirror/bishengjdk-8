/*
 * Copyright (c) 2022, Huawei Technologies Co., Ltd. All rights reserved.
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

import org.openeuler.security.openssl.KAEProvider;

import java.nio.charset.StandardCharsets;
import java.security.*;
import java.security.spec.*;
import javax.crypto.Cipher;

/**
 * @test
 * @summary Basic test for RSA
 * @run main RSATest
 */

public class RSATest {
    private static final String algorithm = "RSA";
    private static KeyPairGenerator keyPairGenerator;
    private static byte[] privateKey;
    private static byte[] publicKey;
    private static String plainText = "helloworld";
    //    512, 768,
    private static int[] keySizes = {1024, 2048, 4096, 5120, 6144};
    private static String[] signAlgorithms = {
            "MD2withRSA", "MD5withRSA", "SHA1withRSA", "SHA224withRSA", "SHA256withRSA", "SHA384withRSA", "SHA512withRSA"
    };
    private static String[] signAlgorithmsPSS = {"SHA-1", "SHA-224", "SHA-256", "SHA-384", "SHA-512"};

    public static void main(String[] args) throws Exception {
        Security.insertProviderAt(new KAEProvider(), 1);

        for (int keySize : keySizes) {
            testKeyPairByKeySize(keySize);
            testRSACipher(keySize);
            testSignature();
            testPSSSignature(keySize);
        }
    }

    public static void testKeyPairByKeySize(int keySize) throws Exception {
        keyPairGenerator = KeyPairGenerator.getInstance(algorithm);
        keyPairGenerator.initialize(keySize);
        KeyPair keyPair = keyPairGenerator.generateKeyPair();

        PrivateKey pairPrivate = keyPair.getPrivate();
        PublicKey pairPublic = keyPair.getPublic();

        privateKey = pairPrivate.getEncoded();
        publicKey = pairPublic.getEncoded();
    }

    public static void testRSACipher(int keySize) throws Exception {
        PublicKey pubKey = KeyFactory.getInstance("RSA").generatePublic(new X509EncodedKeySpec(publicKey));
        Cipher cipher = Cipher.getInstance("RSA");
        cipher.init(Cipher.ENCRYPT_MODE, pubKey);

        byte[] cipherText = cipher.doFinal(plainText.getBytes(StandardCharsets.UTF_8));

        PrivateKey priKey = KeyFactory.getInstance("RSA").generatePrivate(new PKCS8EncodedKeySpec(privateKey));

        cipher.init(Cipher.DECRYPT_MODE, priKey);

        String decryptText = new String(cipher.doFinal(cipherText));

        if (!plainText.equals(decryptText)) {
            throw new RuntimeException("rsa decryption failed. keySize = " + keySize);
        }
    }

    public static void testSignature() throws Exception {
        PrivateKey priKey = KeyFactory.getInstance("RSA").generatePrivate(new PKCS8EncodedKeySpec(privateKey));
        PublicKey pubKey = KeyFactory.getInstance("RSA").generatePublic(new X509EncodedKeySpec(publicKey));

        for (String algorithm : signAlgorithms) {
            Signature sign = Signature.getInstance(algorithm);
            sign.initSign(priKey);
            sign.update(plainText.getBytes());
            byte[] signInfo = sign.sign();

            sign.initVerify(pubKey);
            sign.update(plainText.getBytes());
            if (!sign.verify(signInfo)) {
                throw new RuntimeException("rsa testSignature failed. digest algorithm = " + algorithm);
            }
        }
    }

    public static void testPSSSignature(int keySize) throws Exception {
        PrivateKey priKey = KeyFactory.getInstance("RSA").generatePrivate(new PKCS8EncodedKeySpec(privateKey));
        PublicKey pubKey = KeyFactory.getInstance("RSA").generatePublic(new X509EncodedKeySpec(publicKey));

        Signature sign = Signature.getInstance("RSASSA-PSS");

        for (String algorithm : signAlgorithmsPSS) {
            if (algorithm.equals(signAlgorithmsPSS[4]) && keySize <= 1024) {
                continue;
            }
            sign.initSign(priKey);

            MessageDigest digest = MessageDigest.getInstance(algorithm);
            byte[] digestByte = digest.digest(plainText.getBytes());
            sign.setParameter(
                    new PSSParameterSpec(algorithm, "MGF1", new MGF1ParameterSpec(algorithm), digestByte.length, 1));

            sign.update(plainText.getBytes());
            byte[] signInfo = sign.sign();

            sign.initVerify(pubKey);

            sign.update(plainText.getBytes());
            if (!sign.verify(signInfo)) {
                throw new RuntimeException("rsa testPSSSignature failed. digest algorithm = " + algorithm);
            }
        }
    }
}
