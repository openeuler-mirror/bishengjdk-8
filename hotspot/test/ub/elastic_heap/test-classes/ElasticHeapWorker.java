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

import java.util.ArrayList;
import java.util.List;
import java.util.Scanner;

public class ElasticHeapWorker {
    private static class MemoryBlock {
        byte[] data;
        int id;
        MemoryBlock(byte[] data, int id) { this.data = data; this.id = id; }
    }

    public static void main(String[] args) throws Exception {
        if (args.length < 1) {
            throw new IllegalArgumentException("Missing allocMb argument");
        }
        int allocMb = Integer.parseInt(args[0]);


        long targetBytes = allocMb * 1024L * 1024L;
        int blockSize = 10 * 1024 * 1024;
        List<MemoryBlock> holders = new ArrayList<>();

        System.out.println("Worker allocate " + allocMb + "MB");
        System.out.println("Worker entering sleep for 5s to wait for jcmd");
        Thread.sleep(5000);

        try {
            long allocated = 0;
            int count = 0;
            while (allocated < targetBytes) {
                int size = (int) Math.min(blockSize, targetBytes - allocated);
                byte[] block = new byte[size];

                for (int i = 0; i < block.length; i++) {
                    block[i] = (byte) ((i ^ count) & 0xFF);
                }

                holders.add(new MemoryBlock(block, count));
                allocated += size;
                count++;
            }
            System.out.println("Allocation Success!");

            System.out.println("Starting Verification...");
            for (MemoryBlock mb : holders) {
                for (int i = 0; i < mb.data.length; i++) {
                    byte expected = (byte) ((i ^ mb.id) & 0xFF);
                    if (mb.data[i] != expected) {
                        throw new RuntimeException("Data Corrupted at block " + mb.id);
                    }
                }
            }
            System.out.println("Verification Success!");
            System.exit(0);
        } catch (OutOfMemoryError e) {
            System.out.println("Allocation Failed: OutOfMemoryError");
            System.exit(1);
        } catch (Exception e) {
            System.out.println("Error: " + e.getMessage());
            System.exit(2);
        }
    }
}
