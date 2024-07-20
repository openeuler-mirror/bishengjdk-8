/*
 * Copyright (c) 2023, Oracle and/or its affiliates. All rights reserved.
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
 * @bug 6956385
 * @summary JarURLConnection may fail to close its underlying FileURLConnection
 * @run main/othervm FileURLConnectionLeak
 */

import java.io.InputStream;
import java.io.OutputStream;
import java.lang.management.ManagementFactory;
import java.lang.management.RuntimeMXBean;
import java.net.URI;
import java.net.URL;
import java.net.URLConnection;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.jar.Attributes;
import java.util.jar.JarOutputStream;
import java.util.jar.Manifest;

public class FileURLConnectionLeak {
    public static void main(String[] args) throws Exception {
        Path jar = Paths.get("x.jar").toAbsolutePath();
        Manifest mani = new Manifest();
        mani.getMainAttributes().put(Attributes.Name.MANIFEST_VERSION, "1.0");
        try (OutputStream os = Files.newOutputStream(jar); OutputStream jos = new JarOutputStream(os, mani)) {}
        URL u = URI.create("jar:" + jar.toUri() + "!/META-INF/MANIFEST.MF").toURL();
        URLConnection urlConnection = u.openConnection();
        urlConnection.setDefaultUseCaches(false);
        // FileURLConnection.is not used, so was always fine:
        try (InputStream is = u.openStream()) {
            byte[] buffer = new byte[1024];
            int bytesRead;
            while ((bytesRead = is.read(buffer)) != -1) {
                System.out.write(buffer, 0, bytesRead);
            }
        }
        // FileURLConnection.is opened implicitly:
        URLConnection conn = u.openConnection();
        conn.getLastModified();
        // Idiom to close URLConnection (cf. JDK-8224095), which must also close the other stream:
        conn.getInputStream().close();
        Path fds = Paths.get("/proc/" + getPid() + "/fd");
        if (Files.isDirectory(fds)) {
            // Linux: verify that x.jar is not open
            for (Path fd : (Iterable<Path>) Files.list(fds)::iterator) {
                if (Files.isSymbolicLink(fd)) {
                    Path file = Files.readSymbolicLink(fd);
                    if (file.equals(jar)) {
                        throw new IllegalStateException("Still held open " + jar + " from " + fd);
                    }
                }
            }
        }
        // Windows: verify that mandatory file locks do not prevent deletion
        Files.delete(jar);
    }

    private static int getPid() {
        try {
            // get runtime MXBean
            RuntimeMXBean runtime = ManagementFactory.getRuntimeMXBean();
            // get pid
            String name = runtime.getName();
            int index = name.indexOf('@');
            if (index != -1) {
                return Integer.parseInt(name.substring(0, index));
            }
        } catch (Exception e) {
            e.printStackTrace();
        }
        return -1;
    }


}
