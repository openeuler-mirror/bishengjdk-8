/*
 * Copyright (c) 2014, 2018, Oracle and/or its affiliates. All rights reserved.
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
 *
 */

/*
 * @test
 * @summary At run time, it is OK to append new elements to the classpath that was used at dump time.
 * @library /testlibrary
 * @compile test-classes/Hello.java
 * @compile test-classes/HelloMore.java
 * @run main AppendClasspath
 */

import java.io.File;
import com.oracle.java.testlibrary.OutputAnalyzer;

public class AppendClasspath {

    public static void main(String[] args) throws Exception {
        String appJar = JarBuilder.getOrCreateHelloJar();
        System.out.println("appJar = ");
        System.out.println(appJar);
        String appJar2 = JarBuilder.build("AppendClasspath_HelloMore", "HelloMore");
        System.out.println("appJar2 = ");
        System.out.println(appJar2);
        // Dump an archive with a specified JAR file in -classpath
        TestCommon.testDump(appJar, TestCommon.list("Hello"));

        // PASS: 1) runtime with classpath containing the one used in dump time
        TestCommon.run(
                "-cp", appJar + File.pathSeparator + appJar2,
                "HelloMore")
                .assertNormalExit();

        // PASS: 2) runtime with classpath different from the one used in dump time
        TestCommon.run(
                "-cp", appJar2 + File.pathSeparator + appJar,
                "HelloMore");

        // PASS: 3) runtime with classpath part of the one used in dump time
        TestCommon.testDump(appJar + File.pathSeparator + appJar2,
                TestCommon.list("Hello"));
        TestCommon.run(
                "-cp", appJar2,
                "Hello");

        // PASS: 4) runtime with same set of jar files in the classpath but
        // with different order
        TestCommon.run(
                "-cp", appJar2 + File.pathSeparator + appJar,
                "HelloMore");
    }
}

