/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
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
 * @test
 * @summary Test NIO socket communication with big data size and multiple clients
 * @library /testlibrary
 * @compile test-classes/NIOServer.java test-classes/NIOClient.java
 * @run main/othervm BigPackageTest
 */

import com.oracle.java.testlibrary.OutputAnalyzer;
import com.oracle.java.testlibrary.ProcessTools;
import java.io.*;
import java.net.*;
import java.nio.file.*;
import java.util.*;

public class BigPackageTest {
    
    private static final int TEST_PORT = 18765;
    private static final int DATA_SIZE = 2 * 1024 * 1024 + 1024;
    private static final int CLIENT_COUNT = 1;
    private static File configFile = null;

    public static void main(String[] args) throws Exception {
        
        try {
            String testDir = System.getProperty("test.classes", ".");
            configFile = new File(testDir, "UBSocket.conf");
            try (BufferedWriter writer = new BufferedWriter(new FileWriter(configFile))) {
                writer.write("NIOServer.main");
                writer.newLine();
                writer.write("NIOClient.main");
                writer.newLine();
            }
            String configPath = configFile.getAbsolutePath();

            ProcessBuilder serverPb = ProcessTools.createJavaProcessBuilder(
                true,
                "-XX:+UnlockExperimentalVMOptions",
                "-XX:+UseUBSocket",
                "-XX:UBConfPath=" + configPath,
                "-XX:+PrintUBLog",
                "NIOServer",
                String.valueOf(TEST_PORT),
                String.valueOf(DATA_SIZE),
                String.valueOf(CLIENT_COUNT)  
            );
            Process serverProcess = serverPb.start();
            
            Thread.sleep(1000);

            List<Process> clientProcesses = new ArrayList<>();
            List<OutputAnalyzer> clientOutputs = new ArrayList<>();
            
            for (int i = 0; i < CLIENT_COUNT; i++) {
                final int clientId = i;
                ProcessBuilder clientPb = ProcessTools.createJavaProcessBuilder(
                    true,
                    "-XX:+UnlockExperimentalVMOptions",
                    "-XX:+UseUBSocket",
                    "-XX:UBConfPath=" + configPath,
                    "-XX:+PrintUBLog",
                    "NIOClient",
                    "localhost",
                    String.valueOf(TEST_PORT),
                    String.valueOf(DATA_SIZE),
                    "Client-" + clientId
                );
                Process clientProcess = clientPb.start();
                clientProcesses.add(clientProcess);
                
                System.out.println("Started Client-" + clientId);
            }

            for (int i = 0; i < CLIENT_COUNT; i++) {
                OutputAnalyzer output = new OutputAnalyzer(clientProcesses.get(i));
                clientOutputs.add(output);
                
                output.shouldHaveExitValue(0);
                // APP Log
                output.shouldContain("Data sent successfully");
                // UB Debug Log
                output.shouldContain("send heartbeat");
                
                System.out.println("=== Client-" + i + " Output ===");
                System.out.println(output.getOutput());
            }

            OutputAnalyzer serverOutput = new OutputAnalyzer(serverProcess);
            serverOutput.shouldHaveExitValue(0);
            // APP Log
            serverOutput.shouldContain("All " + CLIENT_COUNT + " clients completed successfully");
            // UB Debug Log
            serverOutput.shouldContain("Heartbeat");
            
            System.out.println("=== Server Output ===");
            System.out.println(serverOutput.getOutput());
        } finally {
            if (configFile != null && configFile.exists()) {
                boolean deleted = configFile.delete();
                System.out.println("Cleanup: UBSocket.conf deleted = " + deleted);
            }
        }
    }
}