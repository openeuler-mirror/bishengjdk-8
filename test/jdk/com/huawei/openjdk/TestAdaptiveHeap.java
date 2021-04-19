package com.huawei.openjdk.adaptiveheap;
 /**
  * @test TestAdaptiveHeap.java
  * @key gc
  * @library /testlibrary
  * @build com.huawei.openjdk.adaptiveheap.TestAdaptiveHeap
  * @run main/othervm  com.huawei.openjdk.adaptiveheap.TestAdaptiveHeap  -Xms16G -Xmx16G -XX:+UnlockExperimentalVMOptions -XX:+UseG1GC -XX:G1PeriodicGCLoadThreshold=20 -XX:G1PeriodicGCInterval=15000 -XX:+G1Uncommit
  * @summary test adaptheap
  * @author wangruishun
  */

import com.oracle.java.testlibrary.OutputAnalyzer;
import com.oracle.java.testlibrary.ProcessTools;

public class TestAdaptiveHeap {

    public static void main(String[] args)throws Exception {
        final String[] arguments = {
                "-Xbootclasspath/a:.",
                "-Xmx16G",
                ExeTest.class.getName(),
                args[0],
                args[1],
                args[2],
                args[3],
                args[4],
                args[5],
                args[6]
        };

        ProcessBuilder pb = ProcessTools.createJavaProcessBuilder(arguments);
        OutputAnalyzer output = new OutputAnalyzer(pb.start());
        output.shouldHaveExitValue(0);
        System.out.println();
    }

    private static class ExeTest {
	public static void main(String[] str){
		System.out.println();
	}
    }
}
