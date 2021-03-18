/*
 * Copyright (c) 1997, 2020, Oracle and/or its affiliates. All rights reserved.
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

package org.openeuler.security.openssl;

import java.io.BufferedWriter;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardOpenOption;
import java.util.Date;
import java.security.Provider;

/**
 * KAE Provider
 */
public class KAEProvider extends Provider {
    private static Throwable excp;
    private static boolean needLog = true;

    static {
        Throwable status = null;
        try {
            System.loadLibrary("j2kae");
            initOpenssl();
        } catch (UnsatisfiedLinkError t) {
            status = t;
        } catch (RuntimeException e) {
            status = e;
        }
        excp = status;
    }

    private void logStart(Throwable excp) {
        File file = new File(System.getProperty("user.dir"), "kae.log");
        Path fpath = file.toPath();
        if (!Files.exists(fpath)) {
            try {
                file.createNewFile();
            } catch (IOException e) {
                e.printStackTrace();
            }
        }

        try (BufferedWriter writer = Files.newBufferedWriter(fpath, StandardOpenOption.APPEND)) {
            if (excp != null) {
                writer.write(excp.getMessage());
            } else {
                writer.write("KAE Engine was found");
            }
            writer.write("    " + new Date());
            writer.newLine();
        } catch (IOException e) {
            e.initCause(excp).printStackTrace();
        }
        KAEProvider.excp = null; // Exception already logged, clean it.
    }

    private void putCipherAES() {
        final String blockModes = "ECB|CBC|CTR";
        final String blockPads = "NOPADDING|PKCS5PADDING";

        put("Cipher.AES SupportedModes", blockModes);
        put("Cipher.AES SupportedPaddings", blockPads);
        put("Cipher.AES", "org.openeuler.security.openssl.KAEAESCipher$Aes$Ecb$PKCS5Padding");

        put("Cipher.AES/CBC/PKCS5Padding", "org.openeuler.security.openssl.KAEAESCipher$Aes$Cbc$PKCS5Padding");
        put("Cipher.AES/CBC/NoPadding", "org.openeuler.security.openssl.KAEAESCipher$Aes$Cbc$NoPadding");
        put("Alg.Alias.Cipher.AES/CBC/PKCS7Padding", "AES/CBC/PKCS5Padding");
        put("Cipher.AES/ECB/NoPadding", "org.openeuler.security.openssl.KAEAESCipher$Aes$Ecb$NoPadding");
        put("Cipher.AES/ECB/PKCS5Padding", "org.openeuler.security.openssl.KAEAESCipher$Aes$Ecb$PKCS5Padding");
        put("Alg.Alias.Cipher.AES/ECB/PKCS7Padding", "AES/ECB/PKCS5Padding");
        put("Cipher.AES/CTR/NoPadding", "org.openeuler.security.openssl.KAEAESCipher$Aes$Ctr$NoPadding");

        put("Cipher.AES_128/CBC/PKCS5Padding", "org.openeuler.security.openssl.KAEAESCipher$Aes_128$Cbc$PKCS5Padding");
        put("Cipher.AES_128/CBC/NoPadding", "org.openeuler.security.openssl.KAEAESCipher$Aes_128$Cbc$NoPadding");
        put("Alg.Alias.Cipher.AES_128/CBC/PKCS7Padding", "AES_128/CBC/PKCS5Padding");
        put("Cipher.AES_128/ECB/NoPadding", "org.openeuler.security.openssl.KAEAESCipher$Aes_128$Ecb$NoPadding");
        put("Cipher.AES_128/ECB/PKCS5Padding", "org.openeuler.security.openssl.KAEAESCipher$Aes_128$Ecb$PKCS5Padding");
        put("Alg.Alias.Cipher.AES_128/ECB/PKCS7Padding", "AES_128/ECB/PKCS5Padding");
        put("Cipher.AES_128/CTR/NoPadding", "org.openeuler.security.openssl.KAEAESCipher$Aes_128$Ctr$NoPadding");

        put("Cipher.AES_192/CBC/PKCS5Padding", "org.openeuler.security.openssl.KAEAESCipher$Aes_192$Cbc$PKCS5Padding");
        put("Cipher.AES_192/CBC/NoPadding", "org.openeuler.security.openssl.KAEAESCipher$Aes_192$Cbc$NoPadding");
        put("Alg.Alias.Cipher.AES_192/CBC/PKCS7Padding", "AES_192/CBC/PKCS5Padding");
        put("Cipher.AES_192/ECB/NoPadding", "org.openeuler.security.openssl.KAEAESCipher$Aes_192$Ecb$NoPadding");
        put("Cipher.AES_192/ECB/PKCS5Padding", "org.openeuler.security.openssl.KAEAESCipher$Aes_192$Ecb$PKCS5Padding");
        put("Alg.Alias.Cipher.AES_192/ECB/PKCS7Padding", "AES_192/ECB/PKCS5Padding");
        put("Cipher.AES_192/CTR/NoPadding", "org.openeuler.security.openssl.KAEAESCipher$Aes_192$Ctr$NoPadding");

        put("Cipher.AES_256/CBC/PKCS5Padding", "org.openeuler.security.openssl.KAEAESCipher$Aes_256$Cbc$PKCS5Padding");
        put("Cipher.AES_256/CBC/NoPadding", "org.openeuler.security.openssl.KAEAESCipher$Aes_256$Cbc$NoPadding");
        put("Alg.Alias.Cipher.AES_256/CBC/PKCS7Padding", "AES_256/CBC/PKCS5Padding");
        put("Cipher.AES_256/ECB/NoPadding", "org.openeuler.security.openssl.KAEAESCipher$Aes_256$Ecb$NoPadding");
        put("Cipher.AES_256/ECB/PKCS5Padding", "org.openeuler.security.openssl.KAEAESCipher$Aes_256$Ecb$PKCS5Padding");
        put("Alg.Alias.Cipher.AES_256/ECB/PKCS7Padding", "AES_256/ECB/PKCS5Padding");
        put("Cipher.AES_256/CTR/NoPadding", "org.openeuler.security.openssl.KAEAESCipher$Aes_256$Ctr$NoPadding");
    }

    private void putMessageDigest() {
        put("MessageDigest.MD5", "org.openeuler.security.openssl.KAEDigest$MD5");
        put("MessageDigest.SHA-256", "org.openeuler.security.openssl.KAEDigest$SHA256");
        put("MessageDigest.SHA-384", "org.openeuler.security.openssl.KAEDigest$SHA384");
    }

    private void putCipherRSA() {
        // rsa
        put("KeyPairGenerator.RSA", "org.openeuler.security.openssl.KAERSAKeyPairGenerator$Legacy");
        put("Alg.Alias.KeyPairGenerator.1.2.840.113549.1.1", "RSA");
        put("Alg.Alias.KeyPairGenerator.OID.1.2.840.113549.1.1", "RSA");

        put("KeyPairGenerator.RSASSA-PSS", "org.openeuler.security.openssl.KAERSAKeyPairGenerator$PSS");
        put("Alg.Alias.KeyPairGenerator.1.2.840.113549.1.1.10", "RSASSA-PSS");
        put("Alg.Alias.KeyPairGenerator.OID.1.2.840.113549.1.1.10", "RSASSA-PSS");

        put("Cipher.RSA", "org.openeuler.security.openssl.KAERSACipher");
        put("Cipher.RSA SupportedModes", "ECB");
        put("Cipher.RSA SupportedPaddings",
                "NOPADDING|PKCS1PADDING|OAEPPADDING"
                        + "|OAEPWITHMD5ANDMGF1PADDING"
                        + "|OAEPWITHSHA1ANDMGF1PADDING"
                        + "|OAEPWITHSHA-1ANDMGF1PADDING"
                        + "|OAEPWITHSHA-224ANDMGF1PADDING"
                        + "|OAEPWITHSHA-256ANDMGF1PADDING"
                        + "|OAEPWITHSHA-384ANDMGF1PADDING"
                        + "|OAEPWITHSHA-512ANDMGF1PADDING"
                        + "|OAEPWITHSHA-512/224ANDMGF1PADDING"
                        + "|OAEPWITHSHA-512/256ANDMGF1PADDING");
        put("Cipher.RSA SupportedKeyClasses",
                "java.security.interfaces.RSAPublicKey" +
                        "|java.security.interfaces.RSAPrivateKey");
    }

    private void putMAC() {
        put("MAC.HmacMD5", "org.openeuler.security.openssl.KAEMac$HmacMD5");
        put("MAC.HmacSHA1", "org.openeuler.security.openssl.KAEMac$HmacSHA1");
        put("MAC.HmacSHA224", "org.openeuler.security.openssl.KAEMac$HmacSHA224");
        put("MAC.HmacSHA256", "org.openeuler.security.openssl.KAEMac$HmacSHA256");
        put("MAC.HmacSHA384", "org.openeuler.security.openssl.KAEMac$HmacSHA384");
        put("MAC.HmacSHA512", "org.openeuler.security.openssl.KAEMac$HmacSHA512");
    }

    public KAEProvider() {
        super("KAEProvider", 1.8d, "KAE provider");
        if (needLog) {
            logStart(excp);
            needLog = false; // Log only once
        }
        putMessageDigest();
        putCipherAES();
        putMAC();
        putCipherRSA();
    }

    // init openssl
    static native void initOpenssl() throws RuntimeException;
}
