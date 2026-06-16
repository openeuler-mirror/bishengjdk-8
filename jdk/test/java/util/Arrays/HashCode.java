/*
 * Copyright (c) 2020, 2026, Huawei Technologies Co., Ltd. All rights reserved.
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

/*
 * @test
 * @summary Verify Arrays.hashCode for primitive arrays matches the specified polynomial hash
 * @run main HashCode
 */

import java.util.Arrays;
import java.util.Random;

public class HashCode {
    private static final Random RANDOM = new Random(1);

    public static void main(String[] args) {
        testByte();
        testShort();
        testChar();
        testInt();
    }

    private static void testByte() {
        check(Arrays.hashCode((byte[]) null) == 0, "byte null");
        check(Arrays.hashCode(new byte[0]) == 1, "byte empty");
        check(Arrays.hashCode(new byte[] {1, -3, 7, 11}) == manualHashCode(new byte[] {1, -3, 7, 11}),
              "byte fixed");

        for (int len : interestingLengths()) {
            byte[] a = new byte[len];
            RANDOM.nextBytes(a);
            check(Arrays.hashCode(a) == manualHashCode(a), "byte len=" + len);
        }
    }

    private static void testShort() {
        check(Arrays.hashCode((short[]) null) == 0, "short null");
        check(Arrays.hashCode(new short[0]) == 1, "short empty");
        check(Arrays.hashCode(new short[] {1, -3, 7, 11}) == manualHashCode(new short[] {1, -3, 7, 11}),
              "short fixed");

        for (int len : interestingLengths()) {
            short[] a = new short[len];
            for (int i = 0; i < a.length; i++) {
                a[i] = (short) RANDOM.nextInt();
            }
            check(Arrays.hashCode(a) == manualHashCode(a), "short len=" + len);
        }
    }

    private static void testChar() {
        check(Arrays.hashCode((char[]) null) == 0, "char null");
        check(Arrays.hashCode(new char[0]) == 1, "char empty");
        check(Arrays.hashCode(new char[] {'A', 0, 0xff, 0xffff}) ==
                manualHashCode(new char[] {'A', 0, 0xff, 0xffff}), "char fixed");

        for (int len : interestingLengths()) {
            char[] a = new char[len];
            for (int i = 0; i < a.length; i++) {
                a[i] = (char) RANDOM.nextInt(1 << 16);
            }
            check(Arrays.hashCode(a) == manualHashCode(a), "char len=" + len);
        }
    }

    private static void testInt() {
        check(Arrays.hashCode((int[]) null) == 0, "int null");
        check(Arrays.hashCode(new int[0]) == 1, "int empty");
        check(Arrays.hashCode(new int[] {1, -3, 7, 11}) == manualHashCode(new int[] {1, -3, 7, 11}),
              "int fixed");

        for (int len : interestingLengths()) {
            int[] a = new int[len];
            for (int i = 0; i < a.length; i++) {
                a[i] = RANDOM.nextInt();
            }
            check(Arrays.hashCode(a) == manualHashCode(a), "int len=" + len);
        }
    }

    private static int manualHashCode(byte[] a) {
        int result = 1;
        for (byte e : a) {
            result = 31 * result + e;
        }
        return result;
    }

    private static int manualHashCode(short[] a) {
        int result = 1;
        for (short e : a) {
            result = 31 * result + e;
        }
        return result;
    }

    private static int manualHashCode(char[] a) {
        int result = 1;
        for (char e : a) {
            result = 31 * result + e;
        }
        return result;
    }

    private static int manualHashCode(int[] a) {
        int result = 1;
        for (int e : a) {
            result = 31 * result + e;
        }
        return result;
    }

    private static int[] interestingLengths() {
        return new int[] {0, 1, 2, 3, 4, 7, 8, 15, 16, 31, 32, 63, 64, 127, 128, 255, 256, 511, 512, 1023, 1024};
    }

    private static void check(boolean condition, String message) {
        if (!condition) {
            throw new RuntimeException(message);
        }
    }
}
