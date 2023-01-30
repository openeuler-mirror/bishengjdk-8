/**
 * @test TestNUMANodes.java
 * @library /testlibrary
 * @build CPUSetsReader TestNUMANodes
 * @run main/othervm TestNUMANodes 1 -XX:+UseNUMA -XX:NUMANodes=0 -XX:-LogNUMANodes
 * @run main/othervm TestNUMANodes 2 -XX:+UseNUMA -XX:NUMANodes=all -XX:+LogNUMANodes
 * @run main/othervm TestNUMANodes 3 -XX:+UseNUMA -XX:NUMANodesRandom=1 -XX:+LogNUMANodes
 * @run main/othervm TestNUMANodes 4 -XX:+UseNUMA -XX:NUMANodesRandom=4 -XX:+LogNUMANodes
 * @run main/othervm TestNUMANodes 5 -XX:+UseNUMA -XX:NUMANodes=100-200 -XX:+LogNUMANodes
 * @summary test numanodes
 * @author zhoulei
 */

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Optional;
import java.util.stream.Collectors;
import java.util.stream.IntStream;
import java.util.stream.Stream;
import com.oracle.java.testlibrary.Asserts;
import com.oracle.java.testlibrary.OutputAnalyzer;
import com.oracle.java.testlibrary.ProcessTools;

public class TestNUMANodes {

    private static int getNUMAs() throws Exception {
        final String[] arguments = {"numactl", "-H"};
        OutputAnalyzer output = ProcessTools.executeProcess(new ProcessBuilder(arguments));
        String[] numainfo = output.getStdout().split("\n");
        Optional<String> o = Arrays.asList(numainfo).stream()
                            .filter(line -> line.contains("available"))
                            .findFirst();
        String numas = o.get();
        return Integer.valueOf(numas.substring(11, 12));
    }

    private static class ExeTest {
        public static void main(String[] str) throws Exception {
            int numCpus = CPUSetsReader.getNumCpus();
            String cpuSetStr = CPUSetsReader.readFromProcStatus("Cpus_allowed_list");
            String[] cpus = cpuSetStr.split(",");
            int total = 0;
            for (String cpu : cpus) {
                String[] c = cpu.split("-");
                int start = Integer.valueOf(c[0]);
                int end = Integer.valueOf(c[1]);
                total += end - start + 1;
            }
            System.err.print(total);
        }
    }

    private static OutputAnalyzer forkProcess(String[] args) throws Exception {
        final String[] arguments = {
                    args[1],
                    args[2],
                    args[3],
                    ExeTest.class.getName(),
                    args[0]
            };

        ProcessBuilder pb = ProcessTools.createJavaProcessBuilder(arguments);
        OutputAnalyzer output = new OutputAnalyzer(pb.start());
        output.shouldHaveExitValue(0);
        return output;
    }

    public static void main(String[] args)throws Exception {
        OutputAnalyzer output = forkProcess(args);
        String err = output.getStderr();
        String out = output.getStdout();
        int c = Integer.parseInt(args[0]);
        int numas = TestNUMANodes.getNUMAs();
        int numCpus = CPUSetsReader.getNumCpus();
        switch(c) {
            case 1:
                int cpuUsed = Integer.valueOf(err);
                Asserts.assertTrue(cpuUsed * numas == numCpus);
                break;
            case 2:
                Asserts.assertTrue(err.contains("Mempolicy is not changed"));
                break;
            case 3:
                if (numas > 1) {
                    Asserts.assertTrue(err.contains("NUMANodes is converted to"));
                }
                break;
            case 4:
                Asserts.assertTrue(err.contains("The count of nodes to bind should be"));
                break;
            case 5:
                Asserts.assertTrue(err.contains("is invalid"));
                break;
            default:
                break;
        }
    }
}
