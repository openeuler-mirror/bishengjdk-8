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
import java.security.MessageDigest;
import java.security.Security;

/**
 * @test
 * @summary Basic test for sm3
 * @run main SM3Test
 */

public class SM3Test {

    private static String plainText = "helloworldhellow";

    public static void main(String[] args) throws Exception {
        Security.insertProviderAt(new KAEProvider(), 1);
        test(plainText, "SM3", new byte[]{40, -103, -71, 4, -80, -49, 94, 112, 11, -75, -66, 121, 63, 80, 62, -14, -45, -75, -34, 66, -77, -34, -26, 26, 33, -23, 45, 52, -74, 67, -18, 118});
    }

    public static void test(String plainText, String algo, byte[] expectRes) throws Exception {
       MessageDigest md = MessageDigest.getInstance(algo);
       md.update(plainText.getBytes(StandardCharsets.UTF_8));
       byte[] res = md.digest();
       if (!Arrays.equals(res, expectRes)) {
           throw new RuntimeException("sm3 failed");
       }
    }

}
