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
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.security.NoSuchAlgorithmException;
import java.security.Security;
import javax.crypto.Cipher;
import javax.crypto.spec.IvParameterSpec;
import javax.crypto.spec.SecretKeySpec;

/**
 * @test
 * @summary Basic test for sm4
 * @run main SM4Test
 */

public class SM4Test {

    private static SecretKeySpec ks = new SecretKeySpec("sm4EncryptionKey".getBytes(StandardCharsets.UTF_8), "SM4");  // key has 16 bytes
    private static IvParameterSpec iv = new IvParameterSpec("abcdefghabcdefgh".getBytes(StandardCharsets.UTF_8)); // iv has 16 bytes
    private static IvParameterSpec shortIv = new IvParameterSpec("abcdefgh".getBytes(StandardCharsets.UTF_8)); // CTR support >= 8bytes iv
    private static String plainText = "helloworldhellow";  // 16bytes for NoPadding
    private static String shortPlainText = "helloworld"; // 5 bytes for padding

    public static void main(String[] args) throws Exception {
        Security.insertProviderAt(new KAEProvider(), 1);
        test(plainText, "SM4/CBC/NOPADDING", new byte[]{86, 69, 47, -115, -63, 54, 35, 24, -2, 114, 113, 102, 82, 20, 69, 59});
        test(shortPlainText, "SM4/CBC/PKCS5Padding", new byte[]{10, 105, 75, -80, -85, -68, 13, -53, 42, 91, -64, 99, 104, 35, -85, 8});
        test(plainText, "SM4/ECB/NOPADDING", new byte[]{103, 36, -31, -53, -109, -12, -71, -79, -54, 106, 10, -3, -35, -22, -122, -67});
        test(shortPlainText, "SM4/ECB/PKCS5Padding", new byte[]{-10, 99, -9, 90, 58, -36, -109, 54, -55, -52, 7, -49, 110, -88, 72, 40});
        test(plainText, "SM4/CTR/NOPADDING", new byte[]{32, 108, 35, 108, -16, 119, -111, 114, 94, 110, -100, -113, -46, -29, -11, 71});
        test(plainText, "SM4/OFB/NOPADDING", new byte[]{32, 108, 35, 108, -16, 119, -111, 114, 94, 110, -100, -113, -46, -29, -11, 71});
        test(shortPlainText, "SM4/OFB/PKCS5Padding", new byte[]{32, 108, 35, 108, -16, 119, -111, 114, 94, 110});

        testCtrShortIv(plainText, "SM4/CTR/NOPADDING", new byte[]{-13, 73, 40, -36, -64, -67, 75, -72, 90, 58, 73, -4, -36, 115, 126, -48});
    }

    public static void test(String plainText, String algo, byte[] expectRes) throws Exception {
       Cipher encryptCipher = Cipher.getInstance(algo);
       if (algo.contains("ECB")) {
           encryptCipher.init(Cipher.ENCRYPT_MODE, ks);
       } else {
           encryptCipher.init(Cipher.ENCRYPT_MODE, ks, iv);
       }
       byte[] cipherText = encryptCipher.doFinal(plainText.getBytes(StandardCharsets.UTF_8));
       if (!Arrays.equals(cipherText, expectRes)) {
           throw new RuntimeException("sm4 encryption failed, algo = " + algo);
       }

       Cipher decryptCipher = Cipher.getInstance(algo);
       decryptCipher.init(Cipher.DECRYPT_MODE, ks, encryptCipher.getParameters());
       String decryptPlainText = new String(decryptCipher.doFinal(cipherText));
       if (!plainText.equals(decryptPlainText)) {
           throw new RuntimeException("sm4 decryption failed, algo = " + algo);
       }
    }

    public static void testCtrShortIv(String plainText, String algo, byte[] expectRes) throws Exception {
       Cipher encryptCipher = Cipher.getInstance(algo);
       encryptCipher.init(Cipher.ENCRYPT_MODE, ks, shortIv);
       byte[] cipherText = encryptCipher.doFinal(plainText.getBytes(StandardCharsets.UTF_8));
       if (!Arrays.equals(cipherText, expectRes)) {
           throw new RuntimeException("sm4 encryption failed, algo = " + algo);
       }

       Cipher decryptCipher = Cipher.getInstance(algo);
       decryptCipher.init(Cipher.DECRYPT_MODE, ks, encryptCipher.getParameters());
       String decryptPlainText = new String(decryptCipher.doFinal(cipherText));
       if (!plainText.equals(decryptPlainText)) {
           throw new RuntimeException("sm4 decryption failed, algo = " + algo);
       }
    }
}
