/*
 * Copyright (c) 2019, Red Hat, Inc. All rights reserved.
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

import java.io.*;
import java.nio.file.*;

public class TestGetLoadedClasses {

    private static final int NUM_ITER = 1000;
    private static final int NUM_CLASSES = 10000;

    static {
        try {
            System.loadLibrary("TestGetLoadedClasses");
        } catch (UnsatisfiedLinkError ule) {
            System.err.println("Could not load TestGetLoadedClasses library");
            System.err.println("java.library.path: "
                    + System.getProperty("java.library.path"));
            throw ule;
        }
    }

    native static int getLoadedClasses();

    static Class[] classes = new Class[NUM_CLASSES];

    static class Dummy {
    }

    static class MyClassLoader extends ClassLoader {
        final String path;

        MyClassLoader(String path) {
            this.path = path;
        }

        public Class<?> loadClass(String name) throws ClassNotFoundException {
            try {
                File f = new File(path, name + ".class");
                if (!f.exists()) {
                    return super.loadClass(name);
                }

                Path path = Paths.get(f.getAbsolutePath());
                byte[] cls = Files.readAllBytes(path);
                return defineClass(name, cls, 0, cls.length, null);
            } catch (IOException e) {
                throw new ClassNotFoundException(name);
            }
        }
    }

    static Class load(String path) throws Exception {
        ClassLoader cl = new MyClassLoader(path);
        Class<Dummy> c = (Class<Dummy>) Class.forName("TestGetLoadedClasses$Dummy", true, cl);
        if (c.getClassLoader() != cl) {
            throw new IllegalStateException("Should have loaded by target loader");
        }
        return c;
    }

    static void loadClasses() throws Exception {
        String classDir = TestGetLoadedClasses.class.getProtectionDomain().getCodeSource().getLocation().getPath();
        for (int c = 0; c < NUM_CLASSES; c++) {
            classes[c] = load(classDir);
        }
    }

    public static void main(String args[]) throws Exception {
        loadClasses();
        new TestGetLoadedClasses().run();
    }

    volatile Object sink;
    public void run() throws Exception {
        for (int i = 0; i < NUM_ITER; i++) {
            sink = new byte[1000000];
            int count = getLoadedClasses();
        }
    }
}
