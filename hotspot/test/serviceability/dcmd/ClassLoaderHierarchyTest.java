/*
 * Copyright (c) 2021, Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2014, 2018, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2018, SAP SE. All rights reserved.
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
 */

/*
 * @test
 * @summary Test of diagnostic command VM.classloaders
 * @library /testlibrary
 * @modules java.base/jdk.internal.misc
 *          java.compiler
 *          java.management
 *          jdk.internal.jvmstat/sun.jvmstat.monitor
 * @run testng ClassLoaderHierarchyTest
 */

import org.testng.Assert;
import org.testng.annotations.Test;

import com.oracle.java.testlibrary.OutputAnalyzer;
import com.oracle.java.testlibrary.CommandExecutor;
import com.oracle.java.testlibrary.JMXExecutor;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.channels.FileChannel;

public class ClassLoaderHierarchyTest {

    class EmptyDelegatingLoader extends ClassLoader {
        EmptyDelegatingLoader(ClassLoader parent) {
            super(parent);
        }
    }

    static void loadTestClassInLoaderAndCheck(String classname, ClassLoader loader) throws ClassNotFoundException {
        Class<?> c = Class.forName(classname, true, loader);
        if (c.getClassLoader() != loader) {
            Assert.fail(classname + " defined by wrong classloader: " + c.getClassLoader());
        }
    }

//+-- <bootstrap>
//      |
//      +-- "sun.misc.Launcher$ExtClassLoader", sun.misc.Launcher$ExtClassLoader
//      |     |
//      |     +-- "sun.misc.Launcher$AppClassLoader", sun.misc.Launcher$AppClassLoader
//      |
//      +-- "sun.reflect.DelegatingClassLoader", sun.reflect.DelegatingClassLoader
//      |
//      +-- "ClassLoaderHierarchyTest$TestClassLoader", ClassLoaderHierarchyTest$TestClassLoader
//      |     |
//      |     +-- "ClassLoaderHierarchyTest$TestClassLoader", ClassLoaderHierarchyTest$TestClassLoader
//      |
//      +-- "ClassLoaderHierarchyTest$EmptyDelegatingLoader", ClassLoaderHierarchyTest$EmptyDelegatingLoader
//      |     |
//      |     +-- "ClassLoaderHierarchyTest$EmptyDelegatingLoader", ClassLoaderHierarchyTest$EmptyDelegatingLoader
//      |           |
//      |           +-- "ClassLoaderHierarchyTest$TestClassLoader", ClassLoaderHierarchyTest$TestClassLoader
//      |
//      +-- "ClassLoaderHierarchyTest$EmptyDelegatingLoader", ClassLoaderHierarchyTest$EmptyDelegatingLoader
//            |
//            +-- "ClassLoaderHierarchyTest$EmptyDelegatingLoader", ClassLoaderHierarchyTest$EmptyDelegatingLoader
//                  |
//                  +-- "ClassLoaderHierarchyTest$TestClassLoader", ClassLoaderHierarchyTest$TestClassLoader
//                  |
//                  +-- "ClassLoaderHierarchyTest$TestClassLoader", ClassLoaderHierarchyTest$TestClassLoader
//                  |
//                  +-- "ClassLoaderHierarchyTest$TestClassLoader", ClassLoaderHierarchyTest$TestClassLoader


    public void run(CommandExecutor executor) throws ClassNotFoundException {

        // A) one unnamed, two named loaders
        ClassLoader unnamed_cl = new TestClassLoader(null);
        ClassLoader named_child_cl = new TestClassLoader(unnamed_cl);
        loadTestClassInLoaderAndCheck("TestClass2", unnamed_cl);
        loadTestClassInLoaderAndCheck("TestClass2", named_child_cl);

        // B) A named CL with empty loaders as parents (JDK-8293156)
        EmptyDelegatingLoader emptyLoader1 = new EmptyDelegatingLoader( null);
        EmptyDelegatingLoader emptyLoader2 = new EmptyDelegatingLoader(emptyLoader1);
        ClassLoader named_child_2_cl = new TestClassLoader(emptyLoader2);
        loadTestClassInLoaderAndCheck("TestClass2", named_child_2_cl);

        // C) Test output for several *unnamed* class loaders, same class, same parents,
        //    and all these should be folded by default.
        EmptyDelegatingLoader emptyLoader3 = new EmptyDelegatingLoader(null);
        EmptyDelegatingLoader emptyLoader4 = new EmptyDelegatingLoader(emptyLoader3);
        ClassLoader named_child_3_cl = new TestClassLoader(emptyLoader4); // Same names
        ClassLoader named_child_4_cl = new TestClassLoader(emptyLoader4);
        ClassLoader named_child_5_cl = new TestClassLoader(emptyLoader4);
        loadTestClassInLoaderAndCheck("TestClass2", named_child_3_cl);
        loadTestClassInLoaderAndCheck("TestClass2", named_child_4_cl);
        loadTestClassInLoaderAndCheck("TestClass2", named_child_5_cl);

        // First test: simple output, no classes displayed
        OutputAnalyzer output = executor.execute("VM.classloaders");
        // (A)
        output.shouldContain("+-- <bootstrap>");
        output.shouldContain("      +-- \"sun.misc.Launcher$ExtClassLoader\", sun.misc.Launcher$ExtClassLoader");
        output.shouldContain("      |     +-- \"sun.misc.Launcher$AppClassLoader\", sun.misc.Launcher$AppClassLoader");
        output.shouldContain("      +-- \"sun.reflect.DelegatingClassLoader\", sun.reflect.DelegatingClassLoader");
        output.shouldContain("      +-- \"ClassLoaderHierarchyTest$TestClassLoader\", ClassLoaderHierarchyTest$TestClassLoader");
        output.shouldContain("      |     +-- \"ClassLoaderHierarchyTest$TestClassLoader\", ClassLoaderHierarchyTest$TestClassLoader");
        // (B)
        output.shouldContain("      +-- \"ClassLoaderHierarchyTest$EmptyDelegatingLoader\", ClassLoaderHierarchyTest$EmptyDelegatingLoader");
        output.shouldContain("      |     +-- \"ClassLoaderHierarchyTest$EmptyDelegatingLoader\", ClassLoaderHierarchyTest$EmptyDelegatingLoader");
        output.shouldContain("      |           +-- \"ClassLoaderHierarchyTest$TestClassLoader\", ClassLoaderHierarchyTest$TestClassLoader");
        // (C)
        output.shouldContain("      +-- \"ClassLoaderHierarchyTest$EmptyDelegatingLoader\", ClassLoaderHierarchyTest$EmptyDelegatingLoader");
        output.shouldContain("            +-- \"ClassLoaderHierarchyTest$EmptyDelegatingLoader\", ClassLoaderHierarchyTest$EmptyDelegatingLoader");
        output.shouldContain("                  +-- \"ClassLoaderHierarchyTest$TestClassLoader\", ClassLoaderHierarchyTest$TestClassLoader");

        // Second test: print with classes.
        output = executor.execute("VM.classloaders show-classes");
        output.shouldContain("<bootstrap>");
        output.shouldContain("java.lang.Object");
        output.shouldContain("java.lang.Enum");
        output.shouldContain("java.lang.NullPointerException");
        output.shouldContain("TestClass2");
    }

    static class TestClassLoader extends ClassLoader {

        public TestClassLoader() {
            super();
        }

        public TestClassLoader(ClassLoader parent) {
            super(parent);
        }

        public static final String CLASS_NAME = "TestClass2";

        static ByteBuffer readClassFile(String name)
        {
            File f = new File(System.getProperty("test.classes", "."),
                    name);
            try (FileInputStream fin = new FileInputStream(f);
                 FileChannel fc = fin.getChannel())
            {
                return fc.map(FileChannel.MapMode.READ_ONLY, 0, fc.size());
            } catch (IOException e) {
                Assert.fail("Can't open file: " + name, e);
            }

            /* Will not reach here as Assert.fail() throws exception */
            return null;
        }

        protected Class<?> loadClass(String name, boolean resolve)
                throws ClassNotFoundException
        {
            Class<?> c;
            if (!CLASS_NAME.equals(name)) {
                c = super.loadClass(name, resolve);
            } else {
                // should not delegate to the system class loader
                c = findClass(name);
                if (resolve) {
                    resolveClass(c);
                }
            }
            return c;
        }

        protected Class<?> findClass(String name)
                throws ClassNotFoundException
        {
            if (!CLASS_NAME.equals(name)) {
                throw new ClassNotFoundException("Unexpected class: " + name);
            }
            return defineClass(name, readClassFile(name + ".class"), null);
        }

    }

    @Test
    public void jmx() throws ClassNotFoundException {
        run(new JMXExecutor());
    }

}

class TestClass2 {
    static {
        Runnable r = () -> System.out.println("Hello");
        r.run();
    }
}