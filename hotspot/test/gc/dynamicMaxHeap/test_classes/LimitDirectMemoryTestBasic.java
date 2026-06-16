/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 * Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

import com.oracle.java.testlibrary.JDKToolFinder;
import com.oracle.java.testlibrary.OutputAnalyzer;
import com.oracle.java.testlibrary.ProcessTools;

import java.nio.ByteBuffer;

public class LimitDirectMemoryTestBasic {
    public static void main(String[] args) throws Exception {
        String newSize = args[0];
        int allocSize = Integer.parseInt(args[1]);

        resize(Long.toString(ProcessTools.getProcessId()), newSize);

        try {
            ByteBuffer[] buffers = new ByteBuffer[allocSize];
            for (int i = 0; i < buffers.length; i++) {
                buffers[i] = ByteBuffer.allocateDirect(1024 * 1024);
            }
        } catch (OutOfMemoryError e) {
            System.out.println(e);
            throw e;
        }
        System.out.println("allocation finish!");
    }

    static void resize(String pid, String newSize) throws Exception {
        ProcessBuilder pb = new ProcessBuilder(JDKToolFinder.getJDKTool("jcmd"),
                                               pid,
                                               "GC.elastic_max_direct_memory",
                                               newSize);
        OutputAnalyzer output = new OutputAnalyzer(pb.start());
        System.out.println(output.getOutput());
        output.shouldHaveExitValue(0);
        output.shouldContain("GC.elastic_max_direct_memory success");
    }
}
