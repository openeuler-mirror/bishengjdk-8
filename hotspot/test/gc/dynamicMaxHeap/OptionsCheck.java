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

/*
 * @test OptionsCheck
 * @summary Test invalid options combinations with elastic max heap
 * @library /testlibrary
 * @requires os.family == "linux"
 * @requires os.arch == "aarch64"
 * @build TestBase
 * @run main OptionsCheck
 */

import com.oracle.java.testlibrary.OutputAnalyzer;
import com.oracle.java.testlibrary.ProcessTools;

public class OptionsCheck extends TestBase {
    public static void main(String[] args) throws Exception {
        launchAndCheck(new String[] {"can not be used with"},
                       "-XX:+ElasticMaxHeap", "-Xmn200M", "-version");
        launchAndCheck(new String[] {"can not be used with"},
                       "-XX:+ElasticMaxHeap", "-XX:MaxNewSize=300M", "-version");
        launchAndCheck(new String[] {"can not be used with"},
                       "-XX:+ElasticMaxHeap", "-XX:OldSize=1G", "-version");
        launchAndCheck(new String[] {"should be used with"},
                       "-XX:+ElasticMaxHeap", "-XX:-UseAdaptiveSizePolicy", "-version");
        launchAndCheck(new String[] {"-XX:ElasticMaxHeapSize should be used together with -Xmx/-XX:MaxHeapSize"},
                       "-XX:+ElasticMaxHeap", "-XX:ElasticMaxHeapSize=100M", "-version");
        launchAndCheck(new String[] {"-XX:ElasticMaxHeapSize should be larger than -Xmx/-XX:MaxHeapSize"},
                       "-XX:+ElasticMaxHeap", "-XX:ElasticMaxHeapSize=1G", "-Xmx2G", "-version");
    }

    private static void launchAndCheck(String[] contains, String... command) throws Exception {
        ProcessBuilder pb = ProcessTools.createJavaProcessBuilder(command);
        OutputAnalyzer output = new OutputAnalyzer(pb.start());
        System.out.println(output.getOutput());
        checkOutput(output, contains, null);
    }
}
