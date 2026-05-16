/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 * Copyright (C) 2023, 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

/*
 * @test DirectMemoryBasicTest
 * @summary Test Basic Elastic Max Direct Memory resize
 * @library /testlibrary
 * @requires os.family=="linux"
 * @build TestBase
 * @compile test_classes/NotActiveDirectMemory.java
 * @run main DirectMemoryBasicTest
 */

import com.oracle.java.testlibrary.ProcessTools;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.util.concurrent.TimeUnit;

public class DirectMemoryBasicTest extends TestBase {
    private static final String READY_PREFIX = "NotActiveDirectMemory.pid=";

    public static void main(String[] args) throws Exception {
        test("-XX:+UseParallelGC");
        test("-XX:+UseG1GC");
    }

    private static void test(String heapType) throws Exception {
        ProcessBuilder pb = ProcessTools.createJavaProcessBuilder(heapType,
                                                                  "-XX:+UsePerfData",
                                                                  "-XX:+ElasticMaxDirectMemory",
                                                                  "-Xms100M",
                                                                  "-Xmx100M",
                                                                  "-XX:MaxDirectMemorySize=200M",
                                                                  "NotActiveDirectMemory");
        pb.redirectErrorStream(true);
        Process p = pb.start();
        StringBuilder targetOutput = new StringBuilder();
        BufferedReader reader = new BufferedReader(new InputStreamReader(p.getInputStream()));
        try {
            String pid = waitForPid(reader, targetOutput, READY_PREFIX);

            resizeAndCheck(pid,
                           "300M",
                           new String[] {"GC.elastic_max_direct_memory (",
                                         "GC.elastic_max_direct_memory success"},
                           null,
                           "GC.elastic_max_direct_memory");
            resizeAndCheck(pid,
                           "50M",
                           new String[] {"below current reserved direct memory",
                                         "GC.elastic_max_direct_memory fail"},
                           null,
                           "GC.elastic_max_direct_memory");
        } finally {
            p.destroy();
            if (!p.waitFor(10, TimeUnit.SECONDS)) {
                p.destroyForcibly();
                p.waitFor();
            }
            drain(reader, targetOutput);
            System.out.println(targetOutput.toString());
        }
    }
}
