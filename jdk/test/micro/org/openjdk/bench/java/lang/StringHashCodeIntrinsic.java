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
package org.openjdk.bench.java.lang;

import org.openjdk.jmh.annotations.Benchmark;
import org.openjdk.jmh.annotations.BenchmarkMode;
import org.openjdk.jmh.annotations.Fork;
import org.openjdk.jmh.annotations.Level;
import org.openjdk.jmh.annotations.Measurement;
import org.openjdk.jmh.annotations.Mode;
import org.openjdk.jmh.annotations.OutputTimeUnit;
import org.openjdk.jmh.annotations.Param;
import org.openjdk.jmh.annotations.Scope;
import org.openjdk.jmh.annotations.Setup;
import org.openjdk.jmh.annotations.State;
import org.openjdk.jmh.annotations.Warmup;

import java.lang.reflect.Field;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.TimeUnit;

/**
 * Measures the uncached {@code String.hashCode()} path on long Latin1/UTF16 inputs
 * while toggling the vectorized intrinsic on and off.
 */
@BenchmarkMode(Mode.AverageTime)
@OutputTimeUnit(TimeUnit.NANOSECONDS)
@Warmup(iterations = 5, time = 1)
@Measurement(iterations = 5, time = 1)
@State(Scope.Thread)
public class StringHashCodeIntrinsic {

    private static volatile Field hashField;

    @Param({"64", "1024", "4096"})
    private int size;

    private String latin1;
    private String utf16;

    @Setup(Level.Trial)
    public void setup() throws ReflectiveOperationException {
        initHashField();
        latin1 = latin1String(size);
        utf16 = utf16String(size);
        verifyPositiveHash(latin1);
        verifyPositiveHash(utf16);
    }

    @Setup(Level.Invocation)
    public void clearHashCache() throws IllegalAccessException {
        resetHash(latin1);
        resetHash(utf16);
    }

    @Benchmark
    @Fork(value = 1, jvmArgsAppend = {
            "-Xbatch",
            "-XX:+UnlockDiagnosticVMOptions",
            "-XX:-UseVectorizedHashCodeIntrinsic"
    })
    public int latin1_noIntrinsic() {
        return latin1.hashCode();
    }

    @Benchmark
    @Fork(value = 1, jvmArgsAppend = {
            "-Xbatch",
            "-XX:+UnlockDiagnosticVMOptions",
            "-XX:+UseVectorizedHashCodeIntrinsic"
    })
    public int latin1_intrinsic() {
        return latin1.hashCode();
    }

    @Benchmark
    @Fork(value = 1, jvmArgsAppend = {
            "-Xbatch",
            "-XX:+UnlockDiagnosticVMOptions",
            "-XX:-UseVectorizedHashCodeIntrinsic"
    })
    public int utf16_noIntrinsic() {
        return utf16.hashCode();
    }

    @Benchmark
    @Fork(value = 1, jvmArgsAppend = {
            "-Xbatch",
            "-XX:+UnlockDiagnosticVMOptions",
            "-XX:+UseVectorizedHashCodeIntrinsic"
    })
    public int utf16_intrinsic() {
        return utf16.hashCode();
    }

    private static void initHashField() throws ReflectiveOperationException {
        if (hashField != null) {
            return;
        }
        synchronized (StringHashCodeIntrinsic.class) {
            if (hashField == null) {
                Field f = String.class.getDeclaredField("hash");
                f.setAccessible(true);
                hashField = f;
            }
        }
    }

    private static void resetHash(String s) throws IllegalAccessException {
        hashField.setInt(s, 0);
    }

    private static void verifyPositiveHash(String s) {
        int hash = s.hashCode();
        if (hash == 0) {
            throw new IllegalStateException("benchmark input unexpectedly hashes to zero");
        }
    }

    private static String latin1String(int size) {
        byte[] value = new byte[size];
        for (int i = 0; i < value.length; i++) {
            value[i] = (byte) ('A' + (i % 23));
        }
        return new String(value, StandardCharsets.ISO_8859_1);
    }

    private static String utf16String(int size) {
        char[] value = new char[size];
        for (int i = 0; i < value.length; i++) {
            value[i] = (char) (0x0100 + (i % 23));
        }
        return new String(value);
    }
}
