/*
 * Copyright (c) 2022, Huawei Technologies Co., Ltd. All rights reserved.
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

package org.openeuler.security.openssl;

import sun.security.util.Debug;

import java.io.BufferedWriter;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.security.AccessController;
import java.security.PrivilegedAction;
import java.text.SimpleDateFormat;
import java.util.Arrays;
import java.util.Date;

public class KAELog {
    private static final Debug kaeDebug = Debug.getInstance("kae");
    private static File logFile;
    private static boolean exist;

    private KAELog() {

    }

    static {
        AccessController.doPrivileged(new PrivilegedAction<Void>() {
            public Void run() {
                initialize();
                return null;
            }
        });
    }

    private static void initialize() {
        if (!enableKaeLog()) {
            if (kaeDebug != null) {
                kaeDebug.println("kae logging is not enabled");
            }
            return;
        }

        logFile = kaeLogFile("kae.log");
        File parentFile = logFile.getParentFile();
        if (!parentFile.exists()) {
            try {
                Files.createDirectories(parentFile.toPath());
            } catch (IOException e) {
                if (kaeDebug != null) {
                    kaeDebug.println("failed to create directory :" + parentFile);
                    e.printStackTrace();
                }
                return;
            }
        }

        if (logFile.exists()) {
            if (kaeDebug != null) {
                kaeDebug.println("found kae log file :" + logFile);
            }
            exist = true;
        } else {
            if (kaeDebug != null) {
                kaeDebug.println("not found kae log file :" + logFile);
            }
            try {
                Path path = Files.createFile(logFile.toPath());
                if (path != null) {
                    exist = true;
                }
            } catch (IOException e) {
                if (kaeDebug != null) {
                    kaeDebug.println("unable to create new kae log file :" + logFile);
                    e.printStackTrace();
                }
            }

            if (exist) {
                if (kaeDebug != null) {
                    kaeDebug.println("create new kae log file :" + logFile);
                }
            }
        }
    }

    public static boolean enableKaeLog() {
        String debug = KAEConfig.privilegedGetOverridable("kae.log");
        return Boolean.parseBoolean(debug);
    }

    private static File kaeLogFile(String filename) {
        String sep = File.separator;
        String defaultKaeLog = System.getProperty("user.dir") + sep + filename;
        String kaeLog = KAEConfig.privilegedGetOverridable("kae.log.file", defaultKaeLog);
        return new File(kaeLog);
    }

    private static String getLogTime() {
        SimpleDateFormat simpleDateFormat = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss");
        return simpleDateFormat.format(new Date());
    }

    public static void log(String engineId, Throwable throwable, boolean[] engineFlags, boolean[] kaeProviderFlags) {
        if (engineFlags.length != kaeProviderFlags.length) {
            if (kaeDebug != null) {
                kaeDebug.println("The length of engineFlags is not equal to the length of kaeProviderFlags.");
                kaeDebug.println(String.format("engineFlags : %s", Arrays.toString(engineFlags)));
                kaeDebug.println(String.format("kaeProviderFlags : %s", Arrays.toString(kaeProviderFlags)));
            }
            return;
        }
        if (!exist) {
            return;
        }

        try (BufferedWriter writer = Files.newBufferedWriter(logFile.toPath(),
                StandardOpenOption.APPEND)) {
            logEngine(writer, engineId, throwable);
            writer.newLine();
            logAlgorithmStrategy(writer, engineFlags, kaeProviderFlags);
            writer.newLine();
        } catch (IOException e) {
            if (kaeDebug != null) {
                kaeDebug.println("write kae log failed");
                e.printStackTrace();
            }
        }
    }

    // log engine
    private static void logEngine(BufferedWriter writer, String engineId, Throwable throwable) throws IOException {
        writer.write(String.format("[%s] ", getLogTime()));
        if (throwable == null) {
            writer.write(String.format("%s engine was found.", engineId));
        } else if (throwable instanceof RuntimeException) {
            writer.write(String.format("%s engine was not found. %s", engineId, throwable.getMessage()));
        } else {
            writer.write(throwable.getMessage());
        }
    }

    // log algorithm strategy
    private static void logAlgorithmStrategy(BufferedWriter writer, boolean[] engineFlags, boolean[] kaeProviderFlags)
            throws IOException {
        writer.write(String.format("[%s] ", getLogTime()));
        writer.write("The implementation strategy of each algorithm is as follows : ");
        for (int i = 0; i < engineFlags.length; i++) {
            writer.newLine();
            String algorithmName = KAEConfig.getAlgorithmName(i);
            String message;
            if (kaeProviderFlags[i]) {
                String detail = engineFlags[i] ? "enable KAE hardware acceleration" : "Use openssl soft calculation";
                message = String.format(" %-11s => %s: %s", algorithmName, "KAEProvider", detail);
            } else {
                message = String.format(" %-11s => %s", algorithmName, "Non-KAEProvider");
            }
            writer.write(message);
        }
    }
}
