/*
 * Copyright (c) 2019, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
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

package jdk.jfr.jcmd;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.stream.Stream;

import jdk.jfr.Recording;
import jdk.test.lib.Asserts;
import jdk.test.lib.process.ProcessTools;

/**
 * @test
 * @bug 8220657
 * @key jfr
 * @library /test/lib /
 * @run main/othervm jdk.jfr.jcmd.TestJcmdDumpWithFileName
 */
public class TestJcmdDumpWithFileName {

    public static void main(String[] args) throws Exception {
        testDumpAll();
        testDumpNamed();
        testDumpNamedWithFilename();
        testDumpNamedWithFilenameExpansion();
    }

    private static void testDumpAll() throws Exception {
        Path p = Paths.get("testDumpAll.jfr").toAbsolutePath();
        try (Recording r = new Recording()) {
            r.setName("testDumpAll");
            r.setDestination(p);
            r.start();

            JcmdHelper.jcmd("JFR.dump");

            Asserts.assertFalse(namedFile(p), "Unexpected file: " + p.toString());
            Asserts.assertTrue(generatedFile(), "Expected generated file");
        }
        cleanup();
    }

    private static void testDumpNamed() throws Exception {
        Path p = Paths.get("testDumpNamed.jfr").toAbsolutePath();
        try (Recording r = new Recording()) {
            r.setName("testDumpNamed");
            r.setDestination(p);
            r.start();

            JcmdHelper.jcmd("JFR.dump", "name=testDumpNamed");

            Asserts.assertTrue(namedFile(p), "Expected file: " + p.toString());
            Asserts.assertFalse(generatedFile(), "Unexpected generated file");
        }
        cleanup();
    }

    private static void testDumpNamedWithFilename() throws Exception {
        Path p = Paths.get("testDumpNamedWithFilename.jfr").toAbsolutePath();
        Path override = Paths.get("override.jfr").toAbsolutePath();
        try (Recording r = new Recording()) {
            r.setName("testDumpNamedWithFilename");
            r.setDestination(p);
            r.start();

            JcmdHelper.jcmd("JFR.dump", "name=testDumpNamedWithFilename", "filename=" + override.toString());

            Asserts.assertFalse(namedFile(p), "Unexpected file: " + p.toString());
            Asserts.assertTrue(namedFile(override), "Expected file: " + override.toString());
            Asserts.assertFalse(generatedFile(), "Unexpected generated file");
        }
        cleanup();
    }
    
    private static void testDumpNamedWithFilenameExpansion() throws Exception {
        long pid = ProcessTools.getProcessId();
        Path dumpPath = Paths.get("dumpPath-%p-%t.jfr").toAbsolutePath();
        try (Recording r = new Recording()) {
            r.setName("testDumpNamedWithFilenameExpansion");
            r.setDestination(dumpPath);
            r.start();
            JcmdHelper.jcmd("JFR.dump", "name=testDumpNamedWithFilenameExpansion", "filename=" + dumpPath.toString());
            Stream<Path> stream = Files.find(Paths.get("."), 1, (s, a) -> s.toString()
                .matches("^.*dumpPath-pid" + pid + ".\\d{4}.\\d{2}.\\d{2}.\\d{2}.\\d{2}.\\d{2}" + ".jfr") && (a.size() > 0L));
            Asserts.assertTrue(stream.findAny().isPresent());
        }
        cleanup();
    }

    private static boolean namedFile(Path dumpFile) throws IOException {
        return Files.exists(dumpFile) && (Files.size(dumpFile) > 0);
    }

    private static boolean generatedFile() throws Exception {
        long pid = ProcessTools.getProcessId();
        try (Stream<Path> stream = Files.find(Paths.get("."), 1, (p, a) -> p.toString()
                .matches("^.*hotspot-pid-" + pid + "-[0-9_]+\\.jfr$") && (a.size() > 0L))) {
            return stream.findAny()
                         .isPresent();
        }
    }

    private static void cleanup() throws IOException {
        try (Stream<Path> stream = Files.find(Paths.get("."), 1, (p, a) -> p.toString().endsWith(".jfr"))) {
            stream.forEach(p -> p.toFile().delete());
        }
    }

}
