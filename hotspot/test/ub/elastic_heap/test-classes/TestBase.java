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

import com.oracle.java.testlibrary.JDKToolFinder;
import com.oracle.java.testlibrary.OutputAnalyzer;
import java.lang.reflect.Field;
import java.util.Arrays;

public class TestBase {

    public static void resizeAndCheck(long pid, String new_size, String[] contains,
                                      String[] not_contains, String type) throws Exception {
        String jcmdPath = JDKToolFinder.getJDKTool("jcmd");

        ProcessBuilder pb = new ProcessBuilder();
        pb.command(new String[] { jcmdPath, Long.toString(pid), type, new_size });

        System.out.printf("[TestBase] Executing: %s %d %s %s%n", jcmdPath, pid, type, new_size);

        OutputAnalyzer output = new OutputAnalyzer(pb.start());

        System.out.println("--- jcmd Command Output Start ---");
        System.out.print(output.getOutput());
        System.out.println("--- jcmd Command Output End ---");

        if (contains != null) {
            for (String s : contains) {
                output.shouldContain(s);
            }
        }

        if (not_contains != null) {
            for (String s : not_contains) {
                output.shouldNotContain(s);
            }
        }
    }

    public static synchronized long getPidOfProcess(Process p) {
        try {
            if (p.getClass().getName().equals("java.lang.UNIXProcess") ||
                    p.getClass().getName().equals("java.lang.ProcessImpl")) {
                Field f = p.getClass().getDeclaredField("pid");
                f.setAccessible(true);
                long pid = f.getLong(p);
                f.setAccessible(false);
                return pid;
            }
        } catch (Exception e) {
            e.printStackTrace();
        }
        return -1;
    }
}