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
 * @summary Test NIO socket communication between two processes with normal data size
 * @library /testlibrary
 * @compile test-classes/NIOServer.java test-classes/NIOClient.java
 * @run main/othervm NIOSocketSingleTest
 */

import com.oracle.java.testlibrary.OutputAnalyzer;
import com.oracle.java.testlibrary.ProcessTools;
import java.io.*;
import java.net.*;
import java.nio.file.*;

public class NIOSocketSingleTest {
    
    private static final int TEST_PORT = 18765;
    private static final int CLIENT_COUNT = 1; 
    private static final int DATA_SIZE = 1024 * 1024;  
    private static File configFile = null;

    public static void main(String[] args) throws Exception {
        System.out.println("Testing with " + CLIENT_COUNT + " clients, " + DATA_SIZE + " bytes each");
        
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
                    "Client"
              );
            Process clientProcess = clientPb.start();
            
            OutputAnalyzer clientOutput = new OutputAnalyzer(clientProcess);
                
            clientOutput.shouldHaveExitValue(0);
            clientOutput.shouldContain("Data sent successfully");
            clientOutput.shouldContain("UB Socket register fd");
            clientOutput.shouldContain("UB Socket buffer read socket");
            clientOutput.shouldContain("UB Socket get free memory");
                
            System.out.println("=== Client" + " Output ===");
            System.out.println(clientOutput.getOutput());

            OutputAnalyzer serverOutput = new OutputAnalyzer(serverProcess);
            serverOutput.shouldHaveExitValue(0);
            serverOutput.shouldContain("All " + CLIENT_COUNT + " clients completed successfully");
            serverOutput.shouldContain("UB Socket register fd");
            serverOutput.shouldContain("UB Socket buffer read socket");
            serverOutput.shouldContain("UB Socket get free memory");
            
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