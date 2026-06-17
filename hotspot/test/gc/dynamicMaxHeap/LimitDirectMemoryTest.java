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
 * @test LimitDirectMemoryTest
 * @summary Test max direct memory can take effect after resize
 * @library /testlibrary
 * @requires os.family=="linux"
 * @build TestBase
 * @compile test_classes/LimitDirectMemoryTestBasic.java
 * @run main LimitDirectMemoryTest
 */

import com.oracle.java.testlibrary.OutputAnalyzer;
import com.oracle.java.testlibrary.ProcessTools;

public class LimitDirectMemoryTest extends TestBase {
    public static void main(String[] args) throws Exception {
        test("-XX:+UseParallelGC", "300M", "300",
             new String[] {"allocation finish!"},
             new String[] {"java.lang.OutOfMemoryError"});
        test("-XX:+UseG1GC", "300M", "300",
             new String[] {"allocation finish!"},
             new String[] {"java.lang.OutOfMemoryError"});

        test("-XX:+UseParallelGC", "50M", "100",
             new String[] {"java.lang.OutOfMemoryError: Direct buffer memory"},
             new String[] {"allocation finish!"});
        test("-XX:+UseG1GC", "50M", "100",
             new String[] {"java.lang.OutOfMemoryError: Direct buffer memory"},
             new String[] {"allocation finish!"});
    }

    private static void test(String heapType,
                             String newSize,
                             String allocSize,
                             String[] contains,
                             String[] notContains) throws Exception {
        ProcessBuilder pb = ProcessTools.createJavaProcessBuilder(heapType,
                                                                  "-Dtest.jdk=" + System.getProperty("test.jdk"),
                                                                  "-XX:+ElasticMaxDirectMemory",
                                                                  "-XX:MaxDirectMemorySize=200M",
                                                                  "-Xms100M",
                                                                  "-Xmx100M",
                                                                  "LimitDirectMemoryTestBasic",
                                                                  newSize,
                                                                  allocSize);
        OutputAnalyzer output = new OutputAnalyzer(pb.start());
        System.out.println(output.getOutput());
        checkOutput(output, contains, notContains);
    }
}
