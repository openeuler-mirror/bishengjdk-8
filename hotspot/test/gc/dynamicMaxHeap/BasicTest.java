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
 * @test BasicTest
 * @summary Test Basic Elastic Max Heap resize
 * @library /testlibrary
 * @requires os.family=="linux"
 * @requires os.arch == "aarch64"
 * @build TestBase
 * @compile test_classes/NotActiveHeap.java
 * @run main BasicTest
 */

import com.oracle.java.testlibrary.ProcessTools;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.util.concurrent.TimeUnit;

public class BasicTest extends TestBase {
    private static final String READY_PREFIX = "NotActiveHeap.pid=";

    public static void main(String[] args) throws Exception {
        test("-XX:+UseParallelGC");
        test("-XX:+UseG1GC");
    }

    private static void test(String heapType) throws Exception {
        ProcessBuilder pb = ProcessTools.createJavaProcessBuilder(heapType,
                                                                  "-XX:+UsePerfData",
                                                                  "-XX:+ElasticMaxHeap",
                                                                  "-Xms104857599",
                                                                  "-Xmx629145599",
                                                                  "-XX:ElasticMaxHeapSize=1073741823",
                                                                  "NotActiveHeap");
        pb.redirectErrorStream(true);
        Process p = pb.start();
        StringBuilder targetOutput = new StringBuilder();
        BufferedReader reader = new BufferedReader(new InputStreamReader(p.getInputStream()));
        try {
            String pid = waitForPid(reader, targetOutput, READY_PREFIX);

            String[] success = {"GC.elastic_max_heap success", "GC.elastic_max_heap ("};
            resizeAndCheck(pid, "500M", success, null);
            resizeAndCheck(pid, "800M", success, null);
            resizeAndCheck(pid, "2G",
                           new String[] {"GC.elastic_max_heap fail", "2097152K exceeds maximum limit"},
                           null);
            resizeAndCheck(pid, "1G", success, null);
            resizeAndCheck(pid, "314572799", success, null);
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
