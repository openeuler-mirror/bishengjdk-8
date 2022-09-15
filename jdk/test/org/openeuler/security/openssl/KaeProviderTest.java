/*
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

import org.openeuler.security.openssl.KAEProvider;

import javax.crypto.Cipher;
import javax.crypto.Mac;
import javax.crypto.NoSuchPaddingException;
import java.security.KeyPairGenerator;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.security.Security;

/**
 * @test
 * @requires os.arch=="aarch64"
 * @summary test for KaeProviderTest
 * @run main/othervm KaeProviderTest
 * @run main/othervm KaeProviderTest true
 * @run main/othervm KaeProviderTest false
 * @run main/othervm KaeProviderTest wrong
 */

public class KaeProviderTest {

    private static final String[] algorithmKaeProviderPropertyNames = new String[]{
            "kae.md5",
            "kae.sha256",
            "kae.sha384",
            "kae.sm3",
            "kae.aes",
            "kae.sm4",
            "kae.hmac",
            "kae.rsa",
            "kae.dh",
            "kae.ec"
    };

    private static final String KAE = "KAEProvider";

    public static void main(String[] args) throws Exception {
        initProperty(args);
        Security.insertProviderAt(new KAEProvider(), 1);
        testALL();
    }

    private static void initProperty(String[] args) {
        if (args.length <= 0) {
            return;
        }
        String value = args[0];
        for (String name : algorithmKaeProviderPropertyNames){
            System.setProperty(name,value);
        }
    }

    public static void testALL() throws Exception {
        testMd5();
        testSha256();
        testSha384();
        testSm3();
        testAes();
        testSm4();
        testHmac();
        testRsa();
        testDh();
        testEc();
    }

    public static void testMd5() throws NoSuchAlgorithmException {
        MessageDigest messageDigest = MessageDigest.getInstance("MD5");
        judge("kae.md5",messageDigest.getProvider().getName());

    }

    public static void testSha256() throws NoSuchAlgorithmException {
        MessageDigest messageDigest = MessageDigest.getInstance("SHA-256");
        judge("kae.sha256",messageDigest.getProvider().getName());
    }

    public static void testSha384() throws NoSuchAlgorithmException {
        MessageDigest messageDigest = MessageDigest.getInstance("SHA-384");
        judge("kae.sha384",messageDigest.getProvider().getName());
    }

    public static void testSm3() throws NoSuchAlgorithmException {
        try{
            MessageDigest messageDigest = MessageDigest.getInstance("SM3");
            judge("kae.sm3",messageDigest.getProvider().getName());
        }catch (NoSuchAlgorithmException e){
            if(Boolean.parseBoolean(System.getProperty("kae.sm3"))){
                throw e;
            }
        }
    }

    public static void testAes() throws NoSuchAlgorithmException, NoSuchPaddingException {
        Cipher cipher = Cipher.getInstance("AES");
        judge("kae.aes",cipher.getProvider().getName());
    }

    public static void testSm4() throws NoSuchAlgorithmException, NoSuchPaddingException {
        try{
            Cipher cipher = Cipher.getInstance("SM4");
            judge("kae.sm4",cipher.getProvider().getName());
        }catch (NoSuchAlgorithmException e){
            if(Boolean.parseBoolean(System.getProperty("kae.sm4"))){
                throw e;
            }
        }
    }

    public static void testHmac() throws NoSuchAlgorithmException {
        Mac mac = Mac.getInstance("HmacMD5");
        judge("kae.hmac",mac.getProvider().getName());
    }

    public static void testRsa() throws NoSuchAlgorithmException, NoSuchPaddingException {
        Cipher cipher = Cipher.getInstance("RSA");
        judge("kae.rsa",cipher.getProvider().getName());
    }

    public static void testDh() throws NoSuchAlgorithmException {
        KeyPairGenerator keyPairGenerator = KeyPairGenerator.getInstance("DH");
        judge("kae.dh",keyPairGenerator.getProvider().getName());
    }

    public static void testEc() throws NoSuchAlgorithmException {
        KeyPairGenerator keyPairGenerator = KeyPairGenerator.getInstance("EC");
        judge("kae.ec",keyPairGenerator.getProvider().getName());
    }

    private static void judge(String algorithm , String providerName){
        String value = System.getProperty(algorithm);
        if (value == null) {
            if (!KAE.equals(providerName)) {
                throw new RuntimeException("KaeProviderTest Failed! default Provider.name is not right!");
            }
        } else {
            if (Boolean.parseBoolean(value) && !KAE.equals(providerName)) {
                throw new RuntimeException("KaeProviderTest Failed! " + algorithm + " is " + value + "," +
                        " Provider.name is not right!");
            }
            if (!Boolean.parseBoolean(value) && KAE.equals(providerName)) {
                throw new RuntimeException("KaeProviderTest Failed! " + algorithm + " is " + value + ", " +
                        " Provider.name is not right!");
            }
        }
    }
}
