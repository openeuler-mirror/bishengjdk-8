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
import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardOpenOption;
import java.util.Date;
import java.util.Properties;
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

    private Properties getProp() {
        Properties props = new Properties();
        String sep = File.separator;
        File propFile = new File(System.getProperty("java.home") + sep + "lib" + sep +
                        "ext" + sep + "kaeprovider.conf");
        if (propFile.exists()) {
            try (InputStream is = new BufferedInputStream(new FileInputStream(propFile))) {
                props.load(is);
            } catch (IOException e) {
                e.printStackTrace();
            }
        }
        return props;
    }

    public KAEProvider() {
        super("KAEProvider", 1.8d, "KAE provider");
        Properties props = getProp();
        if (needLog && "true".equalsIgnoreCase(props.getProperty("kae.log"))) {
            logStart(excp);
            needLog = false; // Log only once
        } else {
            KAEProvider.excp = null; // Ignore exception.
        }
        if (!"false".equalsIgnoreCase(props.getProperty("kae.md5"))) {
            putMD5();
        }
        if (!"false".equalsIgnoreCase(props.getProperty("kae.sha256"))) {
            putSHA256();
        }
        if (!"false".equalsIgnoreCase(props.getProperty("kae.sha384"))) {
            putSHA384();
        }
        if (!"false".equalsIgnoreCase(props.getProperty("kae.sm3"))) {
            putSM3();
        }
        if (!"false".equalsIgnoreCase(props.getProperty("kae.aes"))) {
            putAES();
        }
        if (!"false".equalsIgnoreCase(props.getProperty("kae.sm4"))) {
            putSM4();
        }
        if (!"false".equalsIgnoreCase(props.getProperty("kae.hmac"))) {
            putHMAC();
        }
        if (!"false".equalsIgnoreCase(props.getProperty("kae.rsa"))) {
            putRSA();
            putSignatureRSA();
        }
        if (!"false".equalsIgnoreCase(props.getProperty("kae.dh"))) {
            putDH();
        }
        if (!"false".equalsIgnoreCase(props.getProperty("kae.ec"))) {
            putEC();
        }
    }

    private void putAES() {
        final String blockModes = "ECB|CBC|CTR|GCM";
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
        put("Cipher.AES/GCM/NoPadding", "org.openeuler.security.openssl.KAEAESCipher$Aes$Gcm$NoPadding");
        put("Alg.Alias.Cipher.AES/GCM/PKCS5Padding", "AES/GCM/NoPadding"); // PKCS5Padding -> noPadding

        put("Cipher.AES_128/CBC/PKCS5Padding", "org.openeuler.security.openssl.KAEAESCipher$Aes_128$Cbc$PKCS5Padding");
        put("Cipher.AES_128/CBC/NoPadding", "org.openeuler.security.openssl.KAEAESCipher$Aes_128$Cbc$NoPadding");
        put("Alg.Alias.Cipher.AES_128/CBC/PKCS7Padding", "AES_128/CBC/PKCS5Padding");
        put("Cipher.AES_128/ECB/NoPadding", "org.openeuler.security.openssl.KAEAESCipher$Aes_128$Ecb$NoPadding");
        put("Cipher.AES_128/ECB/PKCS5Padding", "org.openeuler.security.openssl.KAEAESCipher$Aes_128$Ecb$PKCS5Padding");
        put("Alg.Alias.Cipher.AES_128/ECB/PKCS7Padding", "AES_128/ECB/PKCS5Padding");
        put("Cipher.AES_128/CTR/NoPadding", "org.openeuler.security.openssl.KAEAESCipher$Aes_128$Ctr$NoPadding");
        put("Cipher.AES_128/GCM/NoPadding", "org.openeuler.security.openssl.KAEAESCipher$Aes_128$Gcm$NoPadding");
        put("Alg.Alias.Cipher.AES_128/GCM/PKCS5Padding", "AES/GCM/NoPadding");

        put("Cipher.AES_192/CBC/PKCS5Padding", "org.openeuler.security.openssl.KAEAESCipher$Aes_192$Cbc$PKCS5Padding");
        put("Cipher.AES_192/CBC/NoPadding", "org.openeuler.security.openssl.KAEAESCipher$Aes_192$Cbc$NoPadding");
        put("Alg.Alias.Cipher.AES_192/CBC/PKCS7Padding", "AES_192/CBC/PKCS5Padding");
        put("Cipher.AES_192/ECB/NoPadding", "org.openeuler.security.openssl.KAEAESCipher$Aes_192$Ecb$NoPadding");
        put("Cipher.AES_192/ECB/PKCS5Padding", "org.openeuler.security.openssl.KAEAESCipher$Aes_192$Ecb$PKCS5Padding");
        put("Alg.Alias.Cipher.AES_192/ECB/PKCS7Padding", "AES_192/ECB/PKCS5Padding");
        put("Cipher.AES_192/CTR/NoPadding", "org.openeuler.security.openssl.KAEAESCipher$Aes_192$Ctr$NoPadding");
        put("Cipher.AES_192/GCM/NoPadding", "org.openeuler.security.openssl.KAEAESCipher$Aes_192$Gcm$NoPadding");
        put("Alg.Alias.Cipher.AES_192/GCM/PKCS5Padding", "AES/GCM/NoPadding");

        put("Cipher.AES_256/CBC/PKCS5Padding", "org.openeuler.security.openssl.KAEAESCipher$Aes_256$Cbc$PKCS5Padding");
        put("Cipher.AES_256/CBC/NoPadding", "org.openeuler.security.openssl.KAEAESCipher$Aes_256$Cbc$NoPadding");
        put("Alg.Alias.Cipher.AES_256/CBC/PKCS7Padding", "AES_256/CBC/PKCS5Padding");
        put("Cipher.AES_256/ECB/NoPadding", "org.openeuler.security.openssl.KAEAESCipher$Aes_256$Ecb$NoPadding");
        put("Cipher.AES_256/ECB/PKCS5Padding", "org.openeuler.security.openssl.KAEAESCipher$Aes_256$Ecb$PKCS5Padding");
        put("Alg.Alias.Cipher.AES_256/ECB/PKCS7Padding", "AES_256/ECB/PKCS5Padding");
        put("Cipher.AES_256/CTR/NoPadding", "org.openeuler.security.openssl.KAEAESCipher$Aes_256$Ctr$NoPadding");
        put("Cipher.AES_256/GCM/NoPadding", "org.openeuler.security.openssl.KAEAESCipher$Aes_256$Gcm$NoPadding");
        put("Alg.Alias.Cipher.AES_256/GCM/PKCS5Padding", "AES/GCM/NoPadding");
    }

    private void putMD5() {
        put("MessageDigest.MD5", "org.openeuler.security.openssl.KAEDigest$MD5");
    }

    private void putSHA256() {
        put("MessageDigest.SHA-256", "org.openeuler.security.openssl.KAEDigest$SHA256");
    }

    private void putSHA384() {
        put("MessageDigest.SHA-384", "org.openeuler.security.openssl.KAEDigest$SHA384");
    }

    private void putSM3() {
        put("MessageDigest.SM3", "org.openeuler.security.openssl.KAEDigest$SM3");
    }

    private void putSM4() {
        final String blockModes = "ECB|CBC|CTR|OFB";
        final String blockPads = "NOPADDING|PKCS5PADDING";

        put("Cipher.SM4 SupportedModes", blockModes);
        put("Cipher.SM4 SupportedPaddings", blockPads);
        put("Cipher.SM4", "org.openeuler.security.openssl.KAESM4Cipher$Sm4$Ecb$PKCS5Padding");

        put("Cipher.SM4/CBC/PKCS5Padding", "org.openeuler.security.openssl.KAESM4Cipher$Sm4$Cbc$PKCS5Padding");
        put("Cipher.SM4/CBC/NoPadding", "org.openeuler.security.openssl.KAESM4Cipher$Sm4$Cbc$NoPadding");
        put("Alg.Alias.Cipher.SM4/CBC/PKCS7Padding", "SM4/CBC/PKCS5Padding");
        put("Cipher.SM4/ECB/NoPadding", "org.openeuler.security.openssl.KAESM4Cipher$Sm4$Ecb$NoPadding");
        put("Cipher.SM4/ECB/PKCS5Padding", "org.openeuler.security.openssl.KAESM4Cipher$Sm4$Ecb$PKCS5Padding");
        put("Alg.Alias.Cipher.SM4/ECB/PKCS7Padding", "SM4/ECB/PKCS5Padding");
        put("Cipher.SM4/CTR/NoPadding", "org.openeuler.security.openssl.KAESM4Cipher$Sm4$Ctr$NoPadding");
        put("Cipher.SM4/OFB/NoPadding", "org.openeuler.security.openssl.KAESM4Cipher$Sm4$Ofb$NoPadding");
        put("Cipher.SM4/OFB/PKCS5Padding", "org.openeuler.security.openssl.KAESM4Cipher$Sm4$Ofb$PKCS5Padding");
        put("Alg.Alias.Cipher.SM4/OFB/PKCS7Padding", "SM4/OFB/PKCS5Padding");

        put("KeyGenerator.SM4", "com.sun.crypto.provider.AESKeyGenerator");
        put("AlgorithmParameters.SM4", "com.sun.crypto.provider.AESParameters");
    }

    private void putRSA() {
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

    private void putHMAC() {
        put("MAC.HmacMD5", "org.openeuler.security.openssl.KAEHMac$HmacMD5");
        put("MAC.HmacSHA1", "org.openeuler.security.openssl.KAEHMac$HmacSHA1");
        put("MAC.HmacSHA224", "org.openeuler.security.openssl.KAEHMac$HmacSHA224");
        put("MAC.HmacSHA256", "org.openeuler.security.openssl.KAEHMac$HmacSHA256");
        put("MAC.HmacSHA384", "org.openeuler.security.openssl.KAEHMac$HmacSHA384");
        put("MAC.HmacSHA512", "org.openeuler.security.openssl.KAEHMac$HmacSHA512");
    }

    private void putDH() {
        put("KeyPairGenerator.DiffieHellman", "org.openeuler.security.openssl.KAEDHKeyPairGenerator");
        put("Alg.Alias.KeyPairGenerator.DH", "DiffieHellman");
        put("KeyAgreement.DiffieHellman", "org.openeuler.security.openssl.KAEDHKeyAgreement");
        put("Alg.Alias.KeyAgreement.DH", "DiffieHellman");
    }

    private void putSignatureRSA() {
        put("Signature.MD5withRSA",
                "org.openeuler.security.openssl.KAERSASignature$MD5withRSA");
        put("Signature.SHA1withRSA",
                "org.openeuler.security.openssl.KAERSASignature$SHA1withRSA");
        put("Signature.SHA224withRSA",
                "org.openeuler.security.openssl.KAERSASignature$SHA224withRSA");
        put("Signature.SHA256withRSA",
                "org.openeuler.security.openssl.KAERSASignature$SHA256withRSA");
        put("Signature.SHA384withRSA",
                "org.openeuler.security.openssl.KAERSASignature$SHA384withRSA");
        put("Signature.SHA512withRSA",
                "org.openeuler.security.openssl.KAERSASignature$SHA512withRSA");

        // alias
        put("Alg.Alias.Signature.1.2.840.113549.1.1.4",     "MD5withRSA");
        put("Alg.Alias.Signature.OID.1.2.840.113549.1.1.4", "MD5withRSA");

        put("Alg.Alias.Signature.1.2.840.113549.1.1.5",     "SHA1withRSA");
        put("Alg.Alias.Signature.OID.1.2.840.113549.1.1.5", "SHA1withRSA");
        put("Alg.Alias.Signature.1.3.14.3.2.29",            "SHA1withRSA");

        put("Alg.Alias.Signature.1.2.840.113549.1.1.14",     "SHA224withRSA");
        put("Alg.Alias.Signature.OID.1.2.840.113549.1.1.14", "SHA224withRSA");

        put("Alg.Alias.Signature.1.2.840.113549.1.1.11",     "SHA256withRSA");
        put("Alg.Alias.Signature.OID.1.2.840.113549.1.1.11", "SHA256withRSA");

        put("Alg.Alias.Signature.1.2.840.113549.1.1.12",     "SHA384withRSA");
        put("Alg.Alias.Signature.OID.1.2.840.113549.1.1.12", "SHA384withRSA");

        put("Alg.Alias.Signature.1.2.840.113549.1.1.13",     "SHA512withRSA");
        put("Alg.Alias.Signature.OID.1.2.840.113549.1.1.13", "SHA512withRSA");

        put("Signature.RSASSA-PSS", "org.openeuler.security.openssl.KAERSAPSSSignature");

        put("Alg.Alias.Signature.1.2.840.113549.1.1.10",     "RSASSA-PSS");
        put("Alg.Alias.Signature.OID.1.2.840.113549.1.1.10", "RSASSA-PSS");

        // attributes for supported key classes
        String rsaKeyClasses = "java.security.interfaces.RSAPublicKey" +
                "|java.security.interfaces.RSAPrivateKey";
        put("Signature.MD5withRSA SupportedKeyClasses", rsaKeyClasses);
        put("Signature.SHA1withRSA SupportedKeyClasses", rsaKeyClasses);
        put("Signature.SHA224withRSA SupportedKeyClasses", rsaKeyClasses);
        put("Signature.SHA256withRSA SupportedKeyClasses", rsaKeyClasses);
        put("Signature.SHA384withRSA SupportedKeyClasses", rsaKeyClasses);
        put("Signature.SHA512withRSA SupportedKeyClasses", rsaKeyClasses);
        put("Signature.RSASSA-PSS SupportedKeyClasses", rsaKeyClasses);
    }

    private void putEC() {
        put("KeyPairGenerator.EC", "org.openeuler.security.openssl.KAEECKeyPairGenerator");
        put("Alg.Alias.KeyPairGenerator.EllipticCurve", "EC");
        put("KeyAgreement.ECDH", "org.openeuler.security.openssl.KAEECDHKeyAgreement");
    }
    // init openssl
    static native void initOpenssl() throws RuntimeException;
}
