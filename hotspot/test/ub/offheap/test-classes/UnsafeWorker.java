/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
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
 */

import sun.misc.Unsafe;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.List;

public class UnsafeWorker {
    private static Unsafe unsafe;
    private static final long BLOCK_SIZE = 128L * 1024 * 1024;

    static {
        try {
            Field f = Unsafe.class.getDeclaredField("theUnsafe");
            f.setAccessible(true);
            unsafe = (Unsafe) f.get(null);
        } catch (Exception e) { throw new RuntimeException(e); }
    }

    private static void writeValidationPatterns(long baseAddr, long magic) {
        unsafe.putLong(baseAddr, magic);
        unsafe.putLong(baseAddr + BLOCK_SIZE / 2, ~magic);
        unsafe.putLong(baseAddr + BLOCK_SIZE - 8, magic ^ 0xFFFFFFFFFFFFFFFFL);
    }

    private static boolean verifyValidationPatterns(long baseAddr, long magic) {
        boolean headOk = unsafe.getLong(baseAddr) == magic;
        boolean midOk  = unsafe.getLong(baseAddr + BLOCK_SIZE / 2) == ~magic;
        boolean tailOk = unsafe.getLong(baseAddr + BLOCK_SIZE - 8) == (magic ^ 0xFFFFFFFFFFFFFFFFL);
        
        if (!headOk || !midOk || !tailOk) {
            System.err.printf("Data Mismatch! Base:0x%x, Head:%b, Mid:%b, Tail:%b%n",
                    baseAddr, headOk, midOk, tailOk);
            return false;
        }
        return true;
    }

    public static void main(String[] args) {
        long sizeInMb = Long.parseLong(args[0]);
        int numBlocks = (int) sizeInMb / 128;
        List<Long> addresses = new ArrayList<>(numBlocks);

        System.out.println("Allocating " + sizeInMb + "MB...");

        try {
            for (int i = 0; i < numBlocks; i++) {
                long addr = unsafe.allocateMemory(BLOCK_SIZE);
                if (addr == 0) throw new OutOfMemoryError("Unsafe failed to allocate at block " + i);
                addresses.add(addr);
                long magic = 0xABCDEF0000000000L | i;
                writeValidationPatterns(addr, magic);
            }
            System.out.println("Allocation Success!");

            for (int i = 0; i < addresses.size(); i++) {
                long magic = 0xABCDEF0000000000L | i;
                if (!verifyValidationPatterns(addresses.get(i), magic)) {
                    System.err.println("Verification Failed at block index: " + i);
                    System.exit(1);
                }
            }
            System.out.println("Verification Success!");
            System.out.println("UB offheap allocator Test PASSED!");
        } catch (OutOfMemoryError oom) {
            System.exit(2);
        } catch (Throwable t) {
            System.err.println("Test Failed with Exception:");
            t.printStackTrace();
            System.exit(3);
        } finally {
            System.out.println("Cleaning up " + addresses.size() + " blocks...");
            for (long addr : addresses) {
                if (addr != 0) unsafe.freeMemory(addr);
            }
            System.exit(0);
        }
    }
}
