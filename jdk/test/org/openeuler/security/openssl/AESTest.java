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
import java.security.Security;
import java.security.spec.AlgorithmParameterSpec;
import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.IvParameterSpec;

/**
 * @test
 * @summary Basic test for AES
 * @requires os.arch=="aarch64"
 * @run main AESTest
 */

public class AESTest {
    private static final String[] ALGORITHM = {"AES", "AES_128", "AES_192", "AES_256"};
    private static final String[] MODES = {"ECB", "CBC", "CTR", "GCM"};
    private static final String[] PADDING = {"NoPadding", "PKCS5Padding"};
    private static final int AES_128_KEY_LENGTH = 128;
    private static final int AES_192_KEY_LENGTH = 192;
    private static final int AES_256_KEY_LENGTH = 256;
    private static String plainText = "helloworldhellow"; // 16bytes for NoPadding
    private static String shortPlainText = "helloworld"; // 5 bytes for padding

    public static void main(String[] args) throws Exception {
        Security.insertProviderAt(new KAEProvider(), 1);
        for (String algo : ALGORITHM) {
            for (String mode : MODES) {
                int padKinds = 2;
                if (mode.equalsIgnoreCase("CTR")) {
                    padKinds = 1;
                }
                for (int k = 0; k < padKinds; k++) {
                    test(algo, mode, PADDING[k]);
                }
            }
        }
    }

    public static void test(String algo, String mo, String pad) throws Exception {
        AlgorithmParameterSpec aps = null;

        Cipher cipher = Cipher.getInstance(algo + "/" + mo + "/" + pad);

        KeyGenerator kg = KeyGenerator.getInstance("AES");
        if (algo.equalsIgnoreCase("AES_192")) {
            kg.init(AES_192_KEY_LENGTH);
        } else if (algo.equalsIgnoreCase("AES_256")) {
            kg.init(AES_256_KEY_LENGTH);
        } else {
            kg.init(AES_128_KEY_LENGTH);
        }

        SecretKey key = kg.generateKey();

        // encrypt
        if (!mo.equalsIgnoreCase("GCM")) {
            cipher.init(Cipher.ENCRYPT_MODE, key, aps);
        } else {
            cipher.init(Cipher.ENCRYPT_MODE, key);
        }

        String cipherString = null;
        if (!pad.equalsIgnoreCase("NoPadding")) {
            cipherString = shortPlainText;
        } else {
            cipherString = plainText;
        }
        byte[] cipherText = cipher.doFinal(cipherString.getBytes(StandardCharsets.UTF_8));
        if (!mo.equalsIgnoreCase("ECB")) {
            aps = new IvParameterSpec(cipher.getIV());
        } else {
            aps = null;
        }

        if (!mo.equalsIgnoreCase("GCM")) {
            cipher.init(Cipher.DECRYPT_MODE, key, aps);
        } else {
            cipher.init(Cipher.DECRYPT_MODE, key, cipher.getParameters());
        }

        String decryptPlainText = new String(cipher.doFinal(cipherText));

        if (!cipherString.equals(decryptPlainText)) {
            throw new RuntimeException("aes decryption failed, algo = " + algo + ", mo = " + mo + ", pad = " + pad);
        }
    }
}
