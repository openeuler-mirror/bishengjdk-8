/*
 * Copyright (c) 2022, Huawei Technologies Co., Ltd. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Huawei designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Huawei in the LICENSE file that accompanied this code.
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
 * Please visit https://gitee.com/openeuler/bishengjdk-8 if you need additional
 * information or have any questions.
 */

/*
 * @test
 * @bug 7092821
 * @library ../testlibrary
 * @summary make sure that Sun providers do not miss any algorithms after
 *   modifying the frameworks underneath
 * @author Henry Yang
 */

/*
 *- @TestCaseID:Provider/SunJCEValidator.java
 *- @TestCaseName:Provider/SunJCEValidator.java
 *- @TestCaseType:Function test
 *- @RequirementID:AR.SR.IREQ02758058.001.001
 *- @RequirementName: java.security.Provider.getService() is synchronized and became scalability bottleneck
 *- @Condition:JDK8u302 and later
 *- @Brief:Check whether the service provided by the corresponding provider after changing the underlying architecture is different from the original one (subject to openJDK8u302)
 *   -#step:Compare whether the service provided by openJDK8u302 SunJceProvider is consistent with the modified SunJceProvider with this feature
 *- @Expect:Normal Running
 *- @Priority:Level 1
 */

import com.sun.crypto.provider.SunJCE;

import java.security.Provider;

/**
 * validator for SunJCE provider, make sure we do not miss any algorithm
 * after the modification.
 *
 * @author Henry Yang
 * @since 2022-05-05
 */
public class SunJCEValidator extends BaseProviderValidator {
    private static final String OID_PKCS12_RC4_128 = "1.2.840.113549.1.12.1.1";
    private static final String OID_PKCS12_RC4_40 = "1.2.840.113549.1.12.1.2";
    private static final String OID_PKCS12_DESede = "1.2.840.113549.1.12.1.3";
    private static final String OID_PKCS12_RC2_128 = "1.2.840.113549.1.12.1.5";
    private static final String OID_PKCS12_RC2_40 = "1.2.840.113549.1.12.1.6";
    private static final String OID_PKCS5_MD5_DES = "1.2.840.113549.1.5.3";
    private static final String OID_PKCS5_PBKDF2 = "1.2.840.113549.1.5.12";
    private static final String OID_PKCS5_PBES2 = "1.2.840.113549.1.5.13";
    private static final String OID_PKCS3 = "1.2.840.113549.1.3.1";

    public static void main(String[] args) throws Exception {
        SunJCEValidator validator = new SunJCEValidator();
        validator.validate();
    }

    @Override
    Provider getDefaultProvider() {
        return new SunJCE();
    }

    @Override
    boolean validate() throws Exception {
        final String BLOCK_MODES =
                "ECB|CBC|PCBC|CTR|CTS|CFB|OFB"
                        + "|CFB8|CFB16|CFB24|CFB32|CFB40|CFB48|CFB56|CFB64"
                        + "|OFB8|OFB16|OFB24|OFB32|OFB40|OFB48|OFB56|OFB64";
        final String BLOCK_MODES128 =
                BLOCK_MODES
                        + "|GCM|CFB72|CFB80|CFB88|CFB96|CFB104|CFB112|CFB120|CFB128"
                        + "|OFB72|OFB80|OFB88|OFB96|OFB104|OFB112|OFB120|OFB128";
        final String BLOCK_PADS = "NOPADDING|PKCS5PADDING|ISO10126PADDING";

        /*
         * Cipher engines
         */
        checkService("Cipher.RSA");
        checkAttribute("Cipher.RSA SupportedModes", "ECB");
        checkAttribute(
                "Cipher.RSA SupportedPaddings",
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
        checkAttribute(
                "Cipher.RSA SupportedKeyClasses",
                "java.security.interfaces.RSAPublicKey" + "|java.security.interfaces.RSAPrivateKey");

        checkService("Cipher.DES");
        checkAttribute("Cipher.DES SupportedModes", BLOCK_MODES);
        checkAttribute("Cipher.DES SupportedPaddings", BLOCK_PADS);
        checkAttribute("Cipher.DES SupportedKeyFormats", "RAW");

        checkService("Cipher.DESede");
        checkAlias("Alg.Alias.Cipher.TripleDES", "DESede");
        checkAttribute("Cipher.DESede SupportedModes", BLOCK_MODES);
        checkAttribute("Cipher.DESede SupportedPaddings", BLOCK_PADS);
        checkAttribute("Cipher.DESede SupportedKeyFormats", "RAW");

        checkService("Cipher.DESedeWrap");
        checkAttribute("Cipher.DESedeWrap SupportedModes", "CBC");
        checkAttribute("Cipher.DESedeWrap SupportedPaddings", "NOPADDING");
        checkAttribute("Cipher.DESedeWrap SupportedKeyFormats", "RAW");
        System.out.println("Cipher engines check passed");

        // PBES1
        checkService("Cipher.PBEWithMD5AndDES");
        checkAlias("Alg.Alias.Cipher.OID." + OID_PKCS5_MD5_DES, "PBEWithMD5AndDES");
        checkAlias("Alg.Alias.Cipher." + OID_PKCS5_MD5_DES, "PBEWithMD5AndDES");

        checkService("Cipher.PBEWithMD5AndTripleDES");

        checkService("Cipher.PBEWithSHA1AndDESede");
        checkAlias("Alg.Alias.Cipher.OID." + OID_PKCS12_DESede, "PBEWithSHA1AndDESede");
        checkAlias("Alg.Alias.Cipher." + OID_PKCS12_DESede, "PBEWithSHA1AndDESede");

        checkService("Cipher.PBEWithSHA1AndRC2_40");
        checkAlias("Alg.Alias.Cipher.OID." + OID_PKCS12_RC2_40, "PBEWithSHA1AndRC2_40");
        checkAlias("Alg.Alias.Cipher." + OID_PKCS12_RC2_40, "PBEWithSHA1AndRC2_40");

        checkService("Cipher.PBEWithSHA1AndRC2_128");
        checkAlias("Alg.Alias.Cipher.OID." + OID_PKCS12_RC2_128, "PBEWithSHA1AndRC2_128");
        checkAlias("Alg.Alias.Cipher." + OID_PKCS12_RC2_128, "PBEWithSHA1AndRC2_128");

        checkService("Cipher.PBEWithSHA1AndRC4_40");
        checkAlias("Alg.Alias.Cipher.OID." + OID_PKCS12_RC4_40, "PBEWithSHA1AndRC4_40");
        checkAlias("Alg.Alias.Cipher." + OID_PKCS12_RC4_40, "PBEWithSHA1AndRC4_40");

        checkService("Cipher.PBEWithSHA1AndRC4_128");
        checkAlias("Alg.Alias.Cipher.OID." + OID_PKCS12_RC4_128, "PBEWithSHA1AndRC4_128");
        checkAlias("Alg.Alias.Cipher." + OID_PKCS12_RC4_128, "PBEWithSHA1AndRC4_128");
        System.out.println("PBES1 check passed");

        // PBES2

        checkService("Cipher.PBEWithHmacSHA1AndAES_128");

        checkService("Cipher.PBEWithHmacSHA224AndAES_128");

        checkService("Cipher.PBEWithHmacSHA256AndAES_128");

        checkService("Cipher.PBEWithHmacSHA384AndAES_128");

        checkService("Cipher.PBEWithHmacSHA512AndAES_128");

        checkService("Cipher.PBEWithHmacSHA1AndAES_256");

        checkService("Cipher.PBEWithHmacSHA224AndAES_256");

        checkService("Cipher.PBEWithHmacSHA256AndAES_256");

        checkService("Cipher.PBEWithHmacSHA384AndAES_256");

        checkService("Cipher.PBEWithHmacSHA512AndAES_256");

        checkService("Cipher.Blowfish");
        checkAttribute("Cipher.Blowfish SupportedModes", BLOCK_MODES);
        checkAttribute("Cipher.Blowfish SupportedPaddings", BLOCK_PADS);
        checkAttribute("Cipher.Blowfish SupportedKeyFormats", "RAW");

        checkService("Cipher.AES");
        checkAlias("Alg.Alias.Cipher.Rijndael", "AES");
        checkAttribute("Cipher.AES SupportedModes", BLOCK_MODES128);
        checkAttribute("Cipher.AES SupportedPaddings", BLOCK_PADS);
        checkAttribute("Cipher.AES SupportedKeyFormats", "RAW");

        checkService("Cipher.AES_128/ECB/NoPadding");
        checkAlias("Alg.Alias.Cipher.2.16.840.1.101.3.4.1.1", "AES_128/ECB/NoPadding");
        checkAlias("Alg.Alias.Cipher.OID.2.16.840.1.101.3.4.1.1", "AES_128/ECB/NoPadding");
        checkService("Cipher.AES_128/CBC/NoPadding");
        checkAlias("Alg.Alias.Cipher.2.16.840.1.101.3.4.1.2", "AES_128/CBC/NoPadding");
        checkAlias("Alg.Alias.Cipher.OID.2.16.840.1.101.3.4.1.2", "AES_128/CBC/NoPadding");
        checkService("Cipher.AES_128/OFB/NoPadding");
        checkAlias("Alg.Alias.Cipher.2.16.840.1.101.3.4.1.3", "AES_128/OFB/NoPadding");
        checkAlias("Alg.Alias.Cipher.OID.2.16.840.1.101.3.4.1.3", "AES_128/OFB/NoPadding");
        checkService("Cipher.AES_128/CFB/NoPadding");
        checkAlias("Alg.Alias.Cipher.2.16.840.1.101.3.4.1.4", "AES_128/CFB/NoPadding");
        checkAlias("Alg.Alias.Cipher.OID.2.16.840.1.101.3.4.1.4", "AES_128/CFB/NoPadding");
        checkService("Cipher.AES_128/GCM/NoPadding");
        checkAlias("Alg.Alias.Cipher.2.16.840.1.101.3.4.1.6", "AES_128/GCM/NoPadding");
        checkAlias("Alg.Alias.Cipher.OID.2.16.840.1.101.3.4.1.6", "AES_128/GCM/NoPadding");

        checkService("Cipher.AES_192/ECB/NoPadding");
        checkAlias("Alg.Alias.Cipher.2.16.840.1.101.3.4.1.21", "AES_192/ECB/NoPadding");
        checkAlias("Alg.Alias.Cipher.OID.2.16.840.1.101.3.4.1.21", "AES_192/ECB/NoPadding");
        checkService("Cipher.AES_192/CBC/NoPadding");
        checkAlias("Alg.Alias.Cipher.2.16.840.1.101.3.4.1.22", "AES_192/CBC/NoPadding");
        checkAlias("Alg.Alias.Cipher.OID.2.16.840.1.101.3.4.1.22", "AES_192/CBC/NoPadding");
        checkService("Cipher.AES_192/OFB/NoPadding");
        checkAlias("Alg.Alias.Cipher.2.16.840.1.101.3.4.1.23", "AES_192/OFB/NoPadding");
        checkAlias("Alg.Alias.Cipher.OID.2.16.840.1.101.3.4.1.23", "AES_192/OFB/NoPadding");
        checkService("Cipher.AES_192/CFB/NoPadding");
        checkAlias("Alg.Alias.Cipher.2.16.840.1.101.3.4.1.24", "AES_192/CFB/NoPadding");
        checkAlias("Alg.Alias.Cipher.OID.2.16.840.1.101.3.4.1.24", "AES_192/CFB/NoPadding");
        checkService("Cipher.AES_192/GCM/NoPadding");
        checkAlias("Alg.Alias.Cipher.2.16.840.1.101.3.4.1.26", "AES_192/GCM/NoPadding");
        checkAlias("Alg.Alias.Cipher.OID.2.16.840.1.101.3.4.1.26", "AES_192/GCM/NoPadding");

        checkService("Cipher.AES_256/ECB/NoPadding");
        checkAlias("Alg.Alias.Cipher.2.16.840.1.101.3.4.1.41", "AES_256/ECB/NoPadding");
        checkAlias("Alg.Alias.Cipher.OID.2.16.840.1.101.3.4.1.41", "AES_256/ECB/NoPadding");
        checkService("Cipher.AES_256/CBC/NoPadding");
        checkAlias("Alg.Alias.Cipher.2.16.840.1.101.3.4.1.42", "AES_256/CBC/NoPadding");
        checkAlias("Alg.Alias.Cipher.OID.2.16.840.1.101.3.4.1.42", "AES_256/CBC/NoPadding");
        checkService("Cipher.AES_256/OFB/NoPadding");
        checkAlias("Alg.Alias.Cipher.2.16.840.1.101.3.4.1.43", "AES_256/OFB/NoPadding");
        checkAlias("Alg.Alias.Cipher.OID.2.16.840.1.101.3.4.1.43", "AES_256/OFB/NoPadding");
        checkService("Cipher.AES_256/CFB/NoPadding");
        checkAlias("Alg.Alias.Cipher.2.16.840.1.101.3.4.1.44", "AES_256/CFB/NoPadding");
        checkAlias("Alg.Alias.Cipher.OID.2.16.840.1.101.3.4.1.44", "AES_256/CFB/NoPadding");
        checkService("Cipher.AES_256/GCM/NoPadding");
        checkAlias("Alg.Alias.Cipher.2.16.840.1.101.3.4.1.46", "AES_256/GCM/NoPadding");
        checkAlias("Alg.Alias.Cipher.OID.2.16.840.1.101.3.4.1.46", "AES_256/GCM/NoPadding");

        checkService("Cipher.AESWrap");
        checkAttribute("Cipher.AESWrap SupportedModes", "ECB");
        checkAttribute("Cipher.AESWrap SupportedPaddings", "NOPADDING");
        checkAttribute("Cipher.AESWrap SupportedKeyFormats", "RAW");

        checkService("Cipher.AESWrap_128");
        checkAlias("Alg.Alias.Cipher.2.16.840.1.101.3.4.1.5", "AESWrap_128");
        checkAlias("Alg.Alias.Cipher.OID.2.16.840.1.101.3.4.1.5", "AESWrap_128");
        checkService("Cipher.AESWrap_192");
        checkAlias("Alg.Alias.Cipher.2.16.840.1.101.3.4.1.25", "AESWrap_192");
        checkAlias("Alg.Alias.Cipher.OID.2.16.840.1.101.3.4.1.25", "AESWrap_192");
        checkService("Cipher.AESWrap_256");
        checkAlias("Alg.Alias.Cipher.2.16.840.1.101.3.4.1.45", "AESWrap_256");
        checkAlias("Alg.Alias.Cipher.OID.2.16.840.1.101.3.4.1.45", "AESWrap_256");

        checkService("Cipher.RC2");
        checkAttribute("Cipher.RC2 SupportedModes", BLOCK_MODES);
        checkAttribute("Cipher.RC2 SupportedPaddings", BLOCK_PADS);
        checkAttribute("Cipher.RC2 SupportedKeyFormats", "RAW");

        checkService("Cipher.ARCFOUR");
        checkAlias("Alg.Alias.Cipher.RC4", "ARCFOUR");
        checkAttribute("Cipher.ARCFOUR SupportedModes", "ECB");
        checkAttribute("Cipher.ARCFOUR SupportedPaddings", "NOPADDING");
        checkAttribute("Cipher.ARCFOUR SupportedKeyFormats", "RAW");
        System.out.println("PBES2 check passed");

        /*
         * Key(pair) Generator engines
         */
        checkService("KeyGenerator.DES");

        checkService("KeyGenerator.DESede");
        checkAlias("Alg.Alias.KeyGenerator.TripleDES", "DESede");

        checkService("KeyGenerator.Blowfish");

        checkService("KeyGenerator.AES");
        checkAlias("Alg.Alias.KeyGenerator.Rijndael", "AES");

        checkService("KeyGenerator.RC2");
        checkService("KeyGenerator.ARCFOUR");
        checkAlias("Alg.Alias.KeyGenerator.RC4", "ARCFOUR");

        checkService("KeyGenerator.HmacMD5");

        checkService("KeyGenerator.HmacSHA1");
        checkAlias("Alg.Alias.KeyGenerator.OID.1.2.840.113549.2.7", "HmacSHA1");
        checkAlias("Alg.Alias.KeyGenerator.1.2.840.113549.2.7", "HmacSHA1");

        checkService("KeyGenerator.HmacSHA224");
        checkAlias("Alg.Alias.KeyGenerator.OID.1.2.840.113549.2.8", "HmacSHA224");
        checkAlias("Alg.Alias.KeyGenerator.1.2.840.113549.2.8", "HmacSHA224");

        checkService("KeyGenerator.HmacSHA256");
        checkAlias("Alg.Alias.KeyGenerator.OID.1.2.840.113549.2.9", "HmacSHA256");
        checkAlias("Alg.Alias.KeyGenerator.1.2.840.113549.2.9", "HmacSHA256");

        checkService("KeyGenerator.HmacSHA384");
        checkAlias("Alg.Alias.KeyGenerator.OID.1.2.840.113549.2.10", "HmacSHA384");
        checkAlias("Alg.Alias.KeyGenerator.1.2.840.113549.2.10", "HmacSHA384");

        checkService("KeyGenerator.HmacSHA512");
        checkAlias("Alg.Alias.KeyGenerator.OID.1.2.840.113549.2.11", "HmacSHA512");
        checkAlias("Alg.Alias.KeyGenerator.1.2.840.113549.2.11", "HmacSHA512");

        checkService("KeyPairGenerator.DiffieHellman");
        checkAlias("Alg.Alias.KeyPairGenerator.DH", "DiffieHellman");
        checkAlias("Alg.Alias.KeyPairGenerator.OID." + OID_PKCS3, "DiffieHellman");
        checkAlias("Alg.Alias.KeyPairGenerator." + OID_PKCS3, "DiffieHellman");
        System.out.println("Key(pair) Generator engines check passed");

        /*
         * Algorithm parameter generation engines
         */
        checkService("AlgorithmParameterGenerator.DiffieHellman");
        checkAlias("Alg.Alias.AlgorithmParameterGenerator.DH", "DiffieHellman");
        checkAlias("Alg.Alias.AlgorithmParameterGenerator.OID." + OID_PKCS3, "DiffieHellman");
        checkAlias("Alg.Alias.AlgorithmParameterGenerator." + OID_PKCS3, "DiffieHellman");
        System.out.println("Algorithm parameter generation engines check passed");

        /*
         * Key Agreement engines
         */
        checkService("KeyAgreement.DiffieHellman");
        checkAlias("Alg.Alias.KeyAgreement.DH", "DiffieHellman");
        checkAlias("Alg.Alias.KeyAgreement.OID." + OID_PKCS3, "DiffieHellman");
        checkAlias("Alg.Alias.KeyAgreement." + OID_PKCS3, "DiffieHellman");

        checkAttribute(
                "KeyAgreement.DiffieHellman SupportedKeyClasses",
                "javax.crypto.interfaces.DHPublicKey" + "|javax.crypto.interfaces.DHPrivateKey");
        System.out.println("Key Agreement engines check passed");

        /*
         * Algorithm Parameter engines
         */
        checkService("AlgorithmParameters.DiffieHellman");
        checkAlias("Alg.Alias.AlgorithmParameters.DH", "DiffieHellman");
        checkAlias("Alg.Alias.AlgorithmParameters.OID." + OID_PKCS3, "DiffieHellman");
        checkAlias("Alg.Alias.AlgorithmParameters." + OID_PKCS3, "DiffieHellman");

        checkService("AlgorithmParameters.DES");

        checkService("AlgorithmParameters.DESede");
        checkAlias("Alg.Alias.AlgorithmParameters.TripleDES", "DESede");

        checkService("AlgorithmParameters.PBE");

        checkService("AlgorithmParameters.PBEWithMD5AndDES");
        checkAlias("Alg.Alias.AlgorithmParameters.OID." + OID_PKCS5_MD5_DES, "PBEWithMD5AndDES");
        checkAlias("Alg.Alias.AlgorithmParameters." + OID_PKCS5_MD5_DES, "PBEWithMD5AndDES");

        checkService("AlgorithmParameters.PBEWithMD5AndTripleDES");

        checkService("AlgorithmParameters.PBEWithSHA1AndDESede");
        checkAlias("Alg.Alias.AlgorithmParameters.OID." + OID_PKCS12_DESede, "PBEWithSHA1AndDESede");
        checkAlias("Alg.Alias.AlgorithmParameters." + OID_PKCS12_DESede, "PBEWithSHA1AndDESede");

        checkService("AlgorithmParameters.PBEWithSHA1AndRC2_40");
        checkAlias("Alg.Alias.AlgorithmParameters.OID." + OID_PKCS12_RC2_40, "PBEWithSHA1AndRC2_40");
        checkAlias("Alg.Alias.AlgorithmParameters." + OID_PKCS12_RC2_40, "PBEWithSHA1AndRC2_40");

        checkService("AlgorithmParameters.PBEWithSHA1AndRC2_128");
        checkAlias("Alg.Alias.AlgorithmParameters.OID." + OID_PKCS12_RC2_128, "PBEWithSHA1AndRC2_128");
        checkAlias("Alg.Alias.AlgorithmParameters." + OID_PKCS12_RC2_128, "PBEWithSHA1AndRC2_128");

        checkService("AlgorithmParameters.PBEWithSHA1AndRC4_40");
        checkAlias("Alg.Alias.AlgorithmParameters.OID." + OID_PKCS12_RC4_40, "PBEWithSHA1AndRC4_40");
        checkAlias("Alg.Alias.AlgorithmParameters." + OID_PKCS12_RC4_40, "PBEWithSHA1AndRC4_40");

        checkService("AlgorithmParameters.PBEWithSHA1AndRC4_128");
        checkAlias("Alg.Alias.AlgorithmParameters.OID." + OID_PKCS12_RC4_128, "PBEWithSHA1AndRC4_128");
        checkAlias("Alg.Alias.AlgorithmParameters." + OID_PKCS12_RC4_128, "PBEWithSHA1AndRC4_128");

        checkService("AlgorithmParameters.PBES2");
        checkAlias("Alg.Alias.AlgorithmParameters.OID." + OID_PKCS5_PBES2, "PBES2");
        checkAlias("Alg.Alias.AlgorithmParameters." + OID_PKCS5_PBES2, "PBES2");

        checkService("AlgorithmParameters.PBEWithHmacSHA1AndAES_128");

        checkService("AlgorithmParameters.PBEWithHmacSHA224AndAES_128");

        checkService("AlgorithmParameters.PBEWithHmacSHA256AndAES_128");

        checkService("AlgorithmParameters.PBEWithHmacSHA384AndAES_128");

        checkService("AlgorithmParameters.PBEWithHmacSHA512AndAES_128");

        checkService("AlgorithmParameters.PBEWithHmacSHA1AndAES_256");

        checkService("AlgorithmParameters.PBEWithHmacSHA224AndAES_256");

        checkService("AlgorithmParameters.PBEWithHmacSHA256AndAES_256");

        checkService("AlgorithmParameters.PBEWithHmacSHA384AndAES_256");

        checkService("AlgorithmParameters.PBEWithHmacSHA512AndAES_256");

        checkService("AlgorithmParameters.Blowfish");

        checkService("AlgorithmParameters.AES");
        checkAlias("Alg.Alias.AlgorithmParameters.Rijndael", "AES");
        checkService("AlgorithmParameters.GCM");

        checkService("AlgorithmParameters.RC2");

        checkService("AlgorithmParameters.OAEP");
        System.out.println("Algorithm Parameter engines check passed");

        /*
         * Key factories
         */
        checkService("KeyFactory.DiffieHellman");
        checkAlias("Alg.Alias.KeyFactory.DH", "DiffieHellman");
        checkAlias("Alg.Alias.KeyFactory.OID." + OID_PKCS3, "DiffieHellman");
        checkAlias("Alg.Alias.KeyFactory." + OID_PKCS3, "DiffieHellman");
        System.out.println("Key factories check passed");

        /*
         * Secret-key factories
         */
        checkService("SecretKeyFactory.DES");

        checkService("SecretKeyFactory.DESede");
        checkAlias("Alg.Alias.SecretKeyFactory.TripleDES", "DESede");

        checkService("SecretKeyFactory.PBEWithMD5AndDES");
        checkAlias("Alg.Alias.SecretKeyFactory.OID." + OID_PKCS5_MD5_DES, "PBEWithMD5AndDES");
        checkAlias("Alg.Alias.SecretKeyFactory." + OID_PKCS5_MD5_DES, "PBEWithMD5AndDES");

        checkAlias("Alg.Alias.SecretKeyFactory.PBE", "PBEWithMD5AndDES");

        /*
         * Internal in-house crypto algorithm used for
         * the JCEKS keystore type. Since this was developed
         * internally, there isn't an OID corresponding to this
         * algorithm.
         */
        checkService("SecretKeyFactory.PBEWithMD5AndTripleDES");

        checkService("SecretKeyFactory.PBEWithSHA1AndDESede");
        checkAlias("Alg.Alias.SecretKeyFactory.OID." + OID_PKCS12_DESede, "PBEWithSHA1AndDESede");
        checkAlias("Alg.Alias.SecretKeyFactory." + OID_PKCS12_DESede, "PBEWithSHA1AndDESede");

        checkService("SecretKeyFactory.PBEWithSHA1AndRC2_40");
        checkAlias("Alg.Alias.SecretKeyFactory.OID." + OID_PKCS12_RC2_40, "PBEWithSHA1AndRC2_40");
        checkAlias("Alg.Alias.SecretKeyFactory." + OID_PKCS12_RC2_40, "PBEWithSHA1AndRC2_40");

        checkService("SecretKeyFactory.PBEWithSHA1AndRC2_128");
        checkAlias("Alg.Alias.SecretKeyFactory.OID." + OID_PKCS12_RC2_128, "PBEWithSHA1AndRC2_128");
        checkAlias("Alg.Alias.SecretKeyFactory." + OID_PKCS12_RC2_128, "PBEWithSHA1AndRC2_128");

        checkService("SecretKeyFactory.PBEWithSHA1AndRC4_40");

        checkAlias("Alg.Alias.SecretKeyFactory.OID." + OID_PKCS12_RC4_40, "PBEWithSHA1AndRC4_40");
        checkAlias("Alg.Alias.SecretKeyFactory." + OID_PKCS12_RC4_40, "PBEWithSHA1AndRC4_40");

        checkService("SecretKeyFactory.PBEWithSHA1AndRC4_128");

        checkAlias("Alg.Alias.SecretKeyFactory.OID." + OID_PKCS12_RC4_128, "PBEWithSHA1AndRC4_128");
        checkAlias("Alg.Alias.SecretKeyFactory." + OID_PKCS12_RC4_128, "PBEWithSHA1AndRC4_128");

        checkService("SecretKeyFactory.PBEWithHmacSHA1AndAES_128");

        checkService("SecretKeyFactory.PBEWithHmacSHA224AndAES_128");

        checkService("SecretKeyFactory.PBEWithHmacSHA256AndAES_128");

        checkService("SecretKeyFactory.PBEWithHmacSHA384AndAES_128");

        checkService("SecretKeyFactory.PBEWithHmacSHA512AndAES_128");

        checkService("SecretKeyFactory.PBEWithHmacSHA1AndAES_256");

        checkService("SecretKeyFactory.PBEWithHmacSHA224AndAES_256");

        checkService("SecretKeyFactory.PBEWithHmacSHA256AndAES_256");

        checkService("SecretKeyFactory.PBEWithHmacSHA384AndAES_256");

        checkService("SecretKeyFactory.PBEWithHmacSHA512AndAES_256");
        System.out.println("crypto algorithm for JCEKS keystore check passed ");

        // PBKDF2

        checkService("SecretKeyFactory.PBKDF2WithHmacSHA1");
        checkAlias("Alg.Alias.SecretKeyFactory.OID." + OID_PKCS5_PBKDF2, "PBKDF2WithHmacSHA1");
        checkAlias("Alg.Alias.SecretKeyFactory." + OID_PKCS5_PBKDF2, "PBKDF2WithHmacSHA1");

        checkService("SecretKeyFactory.PBKDF2WithHmacSHA224");
        checkService("SecretKeyFactory.PBKDF2WithHmacSHA256");
        checkService("SecretKeyFactory.PBKDF2WithHmacSHA384");
        checkService("SecretKeyFactory.PBKDF2WithHmacSHA512");

        System.out.println("PBKDF2 check passed");

        /*
         * MAC
         */
        checkService("Mac.HmacMD5");
        checkService("Mac.HmacSHA1");
        checkAlias("Alg.Alias.Mac.OID.1.2.840.113549.2.7", "HmacSHA1");
        checkAlias("Alg.Alias.Mac.1.2.840.113549.2.7", "HmacSHA1");
        checkService("Mac.HmacSHA224");
        checkAlias("Alg.Alias.Mac.OID.1.2.840.113549.2.8", "HmacSHA224");
        checkAlias("Alg.Alias.Mac.1.2.840.113549.2.8", "HmacSHA224");
        checkService("Mac.HmacSHA256");
        checkAlias("Alg.Alias.Mac.OID.1.2.840.113549.2.9", "HmacSHA256");
        checkAlias("Alg.Alias.Mac.1.2.840.113549.2.9", "HmacSHA256");
        checkService("Mac.HmacSHA384");
        checkAlias("Alg.Alias.Mac.OID.1.2.840.113549.2.10", "HmacSHA384");
        checkAlias("Alg.Alias.Mac.1.2.840.113549.2.10", "HmacSHA384");
        checkService("Mac.HmacSHA512");
        checkAlias("Alg.Alias.Mac.OID.1.2.840.113549.2.11", "HmacSHA512");
        checkAlias("Alg.Alias.Mac.1.2.840.113549.2.11", "HmacSHA512");
        checkService("Mac.HmacPBESHA1");

        System.out.println("MAC check passed");

        // PBMAC1

        checkService("Mac.PBEWithHmacSHA1");
        checkService("Mac.PBEWithHmacSHA224");
        checkService("Mac.PBEWithHmacSHA256");
        checkService("Mac.PBEWithHmacSHA384");
        checkService("Mac.PBEWithHmacSHA512");

        checkService("Mac.SslMacMD5");
        checkService("Mac.SslMacSHA1");

        checkAttribute("Mac.HmacMD5 SupportedKeyFormats", "RAW");
        checkAttribute("Mac.HmacSHA1 SupportedKeyFormats", "RAW");
        checkAttribute("Mac.HmacSHA224 SupportedKeyFormats", "RAW");
        checkAttribute("Mac.HmacSHA256 SupportedKeyFormats", "RAW");
        checkAttribute("Mac.HmacSHA384 SupportedKeyFormats", "RAW");
        checkAttribute("Mac.HmacSHA512 SupportedKeyFormats", "RAW");
        checkAttribute("Mac.HmacPBESHA1 SupportedKeyFormats", "RAW");
        checkAttribute("Mac.PBEWithHmacSHA1 SupportedKeyFormatS", "RAW");
        checkAttribute("Mac.PBEWithHmacSHA224 SupportedKeyFormats", "RAW");
        checkAttribute("Mac.PBEWithHmacSHA256 SupportedKeyFormats", "RAW");
        checkAttribute("Mac.PBEWithHmacSHA384 SupportedKeyFormats", "RAW");
        checkAttribute("Mac.PBEWithHmacSHA512 SupportedKeyFormats", "RAW");
        checkAttribute("Mac.SslMacMD5 SupportedKeyFormats", "RAW");
        checkAttribute("Mac.SslMacSHA1 SupportedKeyFormats", "RAW");
        System.out.println("PBMAC1 check passed");

        /*
         * KeyStore
         */
        checkService("KeyStore.JCEKS");
        System.out.println("KeyStore check passed");

        /*
         * SSL/TLS mechanisms
         *
         * These are strictly internal implementations and may
         * be changed at any time. These names were chosen
         * because PKCS11/SunPKCS11 does not yet have TLS1.2
         * mechanisms, and it will cause calls to come here.
         */
        checkService("KeyGenerator.SunTlsPrf");
        checkService("KeyGenerator.SunTls12Prf");

        checkService("KeyGenerator.SunTlsMasterSecret");
        checkAlias("Alg.Alias.KeyGenerator.SunTls12MasterSecret", "SunTlsMasterSecret");
        checkAlias("Alg.Alias.KeyGenerator.SunTlsExtendedMasterSecret", "SunTlsMasterSecret");

        checkService("KeyGenerator.SunTlsKeyMaterial");
        checkAlias("Alg.Alias.KeyGenerator.SunTls12KeyMaterial", "SunTlsKeyMaterial");

        checkService("KeyGenerator.SunTlsRsaPremasterSecret");
        checkAlias("Alg.Alias.KeyGenerator.SunTls12RsaPremasterSecret", "SunTlsRsaPremasterSecret");
        System.out.println("SSL/TLS mechanisms check passed");
        return true;
    }
}
