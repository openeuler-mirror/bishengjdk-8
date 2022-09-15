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

import javax.crypto.Mac;
import javax.crypto.spec.SecretKeySpec;
import java.security.Key;
import java.security.Security;
import java.util.Arrays;

/**
 * @test
 * @summary test for Hmac
 * @requires os.arch=="aarch64"
 * @run main/othervm HmacTest
 */
public class HmacTest {
    private static final byte[] PLAIN_BYTES = "hello world".getBytes();
    private static final String[] ALGORITHMS = new String[]{
            "HmacMD5",
            "HmacSHA1",
            "HmacSHA224",
            "HmacSHA256",
            "HmacSHA384",
            "HmacSHA512",
    };
    private static final byte[][] EXPECTED_BYTES = {
            {-40, 63, -96, 13, 107, -33, -1, -53, -116, 117, 75, -6, 85, -88, -112, -90},
            {-68, 104, 112, -36, 123, 123, -92, 104, 89, -90, 63, 56, 84, 45, 12, -7, 41, 103, -105, -27},
            {-31, 0, 103, 51, -119, -61, 2, -76, -83, -113, 95, 86, 8, 46, 91, 20,
                    -15, -23, -71, 62, -50, 86, -54, 71, -94, -47, -103, 43},
            {-69, -83, -3, 7, 61, 38, -122, -59, 7, -53, 106, 114, 58, 102, 65, -118,
                    54, -50, 116, -56, 110, 54, -71, 36, 60, 84, 14, 97, 78, 18, -119, -24},
            {100, -58, 106, 64, -96, 91, 99, -33, 36, -78, -53, -50, -78, 116, -110, 85,
                    84, -5, -63, 17, 51, -69, -39, -122, 65, 8, -122, -43, 39, 13, -41, -52,
                    45, -38, -59, 70, 17, -87, -63, -126, 4, 120, -77, 71, 119, 96, -2, -68},
            {-89, 47, -98, -12, 110, -88, 23, 2, 28, 26, -71, 53, -108, 54, -52, 1,
                    -121, -121, 87, 6, -78, 123, -14, -86, 127, 114, 124, -73, -98, 79, -122, 69,
                    -32, 50, 48, -79, -110, 66, 38, 70, -3, -76, 95, 55, 74, 48, 57, -121,
                    22, 60, -83, -109, 59, 79, 0, -49, 107, 88, -82, -35, 87, -36, 49, -54}
    };
    private static final Key key = new SecretKeySpec("mac".getBytes(), "");

    public static void main(String[] args) throws Exception {
        Security.insertProviderAt(new KAEProvider(), 1);
        for (int i = 0; i < ALGORITHMS.length; i++) {
            test(ALGORITHMS[i], key, PLAIN_BYTES, EXPECTED_BYTES[i]);
        }
    }

    private static void test(String algorithm, Key key, byte[] inputBytes, byte[] expectedBytes) throws Exception {
        Mac mac = Mac.getInstance(algorithm);
        mac.init(key);
        mac.update(inputBytes);
        byte[] bytes = mac.doFinal();
        if (!(mac.getProvider() instanceof KAEProvider)) {
            throw new RuntimeException(algorithm + " failed," +
                    "provider=" + mac.getProvider().getClass() + "," +
                    "expectedProvider=" + KAEProvider.class);
        }
        if (!Arrays.equals(bytes, expectedBytes)) {
            throw new RuntimeException(algorithm + " failed," +
                    "bytes=" + Arrays.toString(bytes) + "," +
                    "expectedBytes=" + Arrays.toString(expectedBytes));
        }
    }
}
