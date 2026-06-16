/*
 * Copyright (c) 2026, Huawei Technologies Co., Ltd. All rights reserved.
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

/* @test
 * @summary Targeted UTF-8 StringCoding coverage for AArch64 UTF conversion intrinsics
 * @requires os.arch == "aarch64"
 * @run main/othervm/timeout=2000 -Xbatch -XX:-TieredCompilation -XX:+UseUTFConversionIntrinsics TestStringCodingUTF8Intrinsics
 */

import java.nio.ByteBuffer;
import java.nio.CharBuffer;
import java.nio.charset.CharsetDecoder;
import java.nio.charset.CharsetEncoder;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.CoderResult;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;

public class TestStringCodingUTF8Intrinsics {
    private static final int ITERATIONS = 20_000;

    public static void main(String[] args) throws Exception {
        testEncodeAscii();
        testEncodeByte1_2();
        testEncodeByte1_3();
        testEncodeScalarFallback();
        testEncodeIllegalSurrogateFallsBack();
        testEncodeBoundaryCodePoints();
        testEncodeThresholdLengths();
        testEncodeDanglingHighSurrogateAtEnd();

        testDecodeAscii();
        testDecodeTwoByteSequence();
        testDecodeThreeByteSequence();
        testDecodeMixedSequences1_2();
        testDecodeMixedSequences1_3();
        testDecodeMixedSequences1_4();
        testDecodeIllegalUtf8FallsBack();
        testDecodeBoundaryCodePoints();
        testDecodeTruncatedUtf8FallsBackToStd();
        testDecodeSplitLeadingAndContinuationRuns();
    }

    private static void testEncodeAscii() throws Exception {
        String s = repeat("ASCII-fast-path-0123456789-", 4);
        assertEncodeCase("encode-ascii-fast-path", s);
    }

    private static void testEncodeByte1_2() throws Exception {
        String s = repeat("Az\u00A2\u07FF", 12);
        assertEncodeCase("encode-1-2-byte-fast-path", s);
    }

    private static void testEncodeByte1_3() throws Exception {
        String s = repeat("A\u00E9\u4F60\u0800", 10);
        assertEncodeCase("encode-1-3-byte-fast-path", s);
    }

    private static void testEncodeScalarFallback() throws Exception {
        String s = repeat("A\u4F60", 6) + "\uD83D\uDE00" + repeat("B\u07FF", 5) + "\uD83D\uDE80Z";
        assertEncodeCase("encode-scalar-fallback-supplementary-tail", s);
    }

    private static void testEncodeIllegalSurrogateFallsBack() throws Exception {
        String s = repeat("A\u4F60", 6) + '\uD800' + repeat("B\u00E9", 8) + '\uDC00';
        assertEncodeCase("encode-illegal-surrogate-fallback", s);
    }

    private static void testEncodeBoundaryCodePoints() throws Exception {
        String s = repeat("\u0000\u007F\u0080\u07FF\u0800\uFFFF", 8)
                + "\uD800\uDC00"
                + "\uDBFF\uDFFF";
        assertEncodeCase("encode-boundary-code-points", s);
    }

    private static void testEncodeThresholdLengths() throws Exception {
        assertEncodeCase("encode-threshold-len-7", repeat("A\u00A2\u4F60", 2) + "Z");
        assertEncodeCase("encode-threshold-len-8", repeat("A\u00A2\u4F60", 2) + "ZW");
        assertEncodeCase("encode-threshold-len-23", repeat("A\u00A2\u4F60", 7) + "YZ");
        assertEncodeCase("encode-threshold-len-24", repeat("A\u00A2\u4F60", 8));
    }

    private static void testEncodeDanglingHighSurrogateAtEnd() throws Exception {
        String s = repeat("A\u00A2\u4F60", 8) + '\uD83D';
        assertEncodeCase("encode-dangling-high-surrogate-at-end", s);
    }

    private static void testDecodeAscii() throws Exception {
        byte[] bytes = asciiBytes(repeat("ASCII-decode-fast-path-0123456789-", 4));
        assertDecodeCase("decode-ascii-fast-path", bytes);
    }

    private static void testDecodeTwoByteSequence() throws Exception {
        byte[] bytes = utf8Bytes(repeat("\u00A2\u07FF", 16));
        assertDecodeCase("decode-2-byte-sequence", bytes);
    }

    private static void testDecodeThreeByteSequence() throws Exception {
        byte[] bytes = utf8Bytes(repeat("\u4F60\u0800\u20AC", 12));
        assertDecodeCase("decode-3-byte-sequence", bytes);
    }

    private static void testDecodeMixedSequences1_2() throws Exception {
        String s = repeat("A\u00A2B\u07FF", 16);
        byte[] bytes = utf8Bytes(s);
        assertDecodeCase("decode-mixed-1-2-byte", bytes);
    }

    private static void testDecodeMixedSequences1_3() throws Exception {
        String s = repeat("A\u00A2\u4F60B\u07FFC\u20AC", 12);
        byte[] bytes = utf8Bytes(s);
        assertDecodeCase("decode-mixed-1-3-byte", bytes);
    }

    private static void testDecodeMixedSequences1_4() throws Exception {
        String s = repeat("A\u00A2\u4F60", 8) + "\uD83D\uDE00"
                + repeat("B\u07FF\u20AC", 7) + "\uD83D\uDE80C";
        byte[] bytes = utf8Bytes(s);
        assertDecodeCase("decode-mixed-1-4-byte", bytes);
    }

    private static void testDecodeIllegalUtf8FallsBack() throws Exception {
        byte[] invalid1 = {
                'A',
                (byte) 0xE2, (byte) 0x28, (byte) 0xA1,
                'B',
                (byte) 0xF0, (byte) 0x28, (byte) 0x8C, (byte) 0xBC,
                'C'
        };
        byte[] invalid2 = {
                'X',
                (byte) 0xC0, (byte) 0xAF,
                'Y',
                (byte) 0xE0, (byte) 0x80, (byte) 0xAF,
                'Z',
                (byte) 0xF4, (byte) 0x90, (byte) 0x80, (byte) 0x80
        };
        byte[] bytes = concat(repeat(invalid1, 6), repeat(invalid2, 5));
        assertDecodeCase("decode-illegal-utf8-fallback", bytes);
    }

    private static void testDecodeBoundaryCodePoints() throws Exception {
        String s = repeat("\u0000\u007F\u0080\u07FF\u0800", 8)
                + "\uFFFD"
                + "\uD800\uDC00"
                + "\uDBFF\uDFFF";
        assertDecodeCase("decode-boundary-code-points", utf8Bytes(s));
    }

    private static void testDecodeTruncatedUtf8FallsBackToStd() throws Exception {
        byte[] bytes = concat(
                utf8Bytes(repeat("A\u00A2\u4F60", 6)),
                new byte[] {
                        (byte) 0xC2,
                        (byte) 0xE2, (byte) 0x82,
                        (byte) 0xF0, (byte) 0x9F, (byte) 0x98
                });
        assertDecodeCase("decode-truncated-utf8-fallback", bytes);
    }

    private static void testDecodeSplitLeadingAndContinuationRuns() throws Exception {
        byte[] bytes = concat(
                utf8Bytes(repeat("A\u00A2\u4F60", 5)),
                concat(
                        new byte[] {
                                (byte) 0x80, (byte) 0x80, (byte) 0x80,
                                (byte) 0xC2, (byte) 0xA2,
                                (byte) 0x80,
                                (byte) 0xE2, (byte) 0x82, (byte) 0xAC
                        },
                        utf8Bytes(repeat("B\uD83D\uDE00C", 4))));
        assertDecodeCase("decode-split-leading-continuation-runs", bytes);
    }

    private static void assertEncodeCase(String name, String s) throws Exception {
        byte[] expected = encodeReference(s);
        for (int i = 0; i < ITERATIONS; i++) {
            byte[] actual = s.getBytes(StandardCharsets.UTF_8);
            if (!Arrays.equals(expected, actual)) {
                failEncode(name, s, expected, actual);
            }
        }
    }

    private static void assertDecodeCase(String name, byte[] bytes) throws Exception {
        String expected = decodeReference(bytes);
        for (int i = 0; i < ITERATIONS; i++) {
            String actual = new String(bytes, StandardCharsets.UTF_8);
            if (!expected.equals(actual)) {
                failDecode(name, bytes, expected, actual);
            }
        }
    }

    private static byte[] encodeReference(String s) throws Exception {
        CharsetEncoder encoder = StandardCharsets.UTF_8.newEncoder();
        encoder.onMalformedInput(CodingErrorAction.REPLACE)
               .onUnmappableCharacter(CodingErrorAction.REPLACE)
               .reset();
        CharBuffer in = CharBuffer.wrap(s);
        byte[] out = new byte[(int) (s.length() * encoder.maxBytesPerChar()) + 8];
        ByteBuffer bb = ByteBuffer.wrap(out);
        CoderResult cr = encoder.encode(in, bb, true);
        if (!cr.isUnderflow()) {
            cr.throwException();
        }
        cr = encoder.flush(bb);
        if (!cr.isUnderflow()) {
            cr.throwException();
        }
        return Arrays.copyOf(out, bb.position());
    }

    private static String decodeReference(byte[] bytes) throws Exception {
        CharsetDecoder decoder = StandardCharsets.UTF_8.newDecoder();
        decoder.onMalformedInput(CodingErrorAction.REPLACE)
               .onUnmappableCharacter(CodingErrorAction.REPLACE)
               .reset();
        ByteBuffer in = ByteBuffer.wrap(bytes);
        char[] out = new char[(int) (bytes.length * decoder.maxCharsPerByte()) + 8];
        CharBuffer cb = CharBuffer.wrap(out);
        CoderResult cr = decoder.decode(in, cb, true);
        if (!cr.isUnderflow()) {
            cr.throwException();
        }
        cr = decoder.flush(cb);
        if (!cr.isUnderflow()) {
            cr.throwException();
        }
        return new String(out, 0, cb.position());
    }

    private static byte[] asciiBytes(String s) {
        return s.getBytes(StandardCharsets.ISO_8859_1);
    }

    private static byte[] utf8Bytes(String s) {
        return s.getBytes(StandardCharsets.UTF_8);
    }

    private static byte[] concat(byte[] left, byte[] right) {
        byte[] out = Arrays.copyOf(left, left.length + right.length);
        System.arraycopy(right, 0, out, left.length, right.length);
        return out;
    }

    private static byte[] repeat(byte[] block, int count) {
        byte[] out = new byte[block.length * count];
        for (int i = 0; i < count; i++) {
            System.arraycopy(block, 0, out, i * block.length, block.length);
        }
        return out;
    }

    private static String repeat(String block, int count) {
        StringBuilder sb = new StringBuilder(block.length() * count);
        for (int i = 0; i < count; i++) {
            sb.append(block);
        }
        return sb.toString();
    }

    private static void failEncode(String name, String input, byte[] expected, byte[] actual) {
        throw new RuntimeException(name
                + " failed, input length=" + input.length()
                + ", expected bytes=" + expected.length
                + ", actual bytes=" + actual.length
                + ", expected=" + Arrays.toString(expected)
                + ", actual=" + Arrays.toString(actual));
    }

    private static void failDecode(String name, byte[] input, String expected, String actual) {
        throw new RuntimeException(name
                + " failed, input bytes=" + Arrays.toString(input)
                + ", expected=\"" + printable(expected) + "\""
                + ", actual=\"" + printable(actual) + "\"");
    }

    private static String printable(String s) {
        StringBuilder sb = new StringBuilder(s.length() * 6);
        for (int i = 0; i < s.length(); i++) {
            sb.append(String.format("\\u%04X", (int) s.charAt(i)));
        }
        return sb.toString();
    }
}
