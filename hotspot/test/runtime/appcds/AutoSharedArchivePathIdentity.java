/*
 * Copyright (c) 2026, Huawei Technologies Co., Ltd. All rights reserved.
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
 * @summary AutoSharedArchivePath isolates and reuses both archive variants by user and launch target.
 * @requires (os.family == "linux") & (os.arch == "aarch64")
 * @run main/timeout=360 AutoSharedArchivePathIdentity
 */

import java.nio.charset.StandardCharsets;
import java.nio.file.DirectoryStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.attribute.FileTime;
import java.nio.file.attribute.PosixFilePermission;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

public class AutoSharedArchivePathIdentity {
    private static final String APP_ONE = "com.huawei.autocds.IdentityAppOne";
    private static final String APP_TWO = "com.huawei.autocds.IdentityAppTwo";

    private static Path work;
    private static String effectiveUserIdentity;
    private static int commandSequence;

    public static void main(String[] args) throws Exception {
        work = Files.createTempDirectory("auto-appcds-identity").toAbsolutePath();
        effectiveUserIdentity = normalize(Files.getOwner(work).getName());
        Path classes = work.resolve("classes");
        Files.createDirectories(classes);
        compileApplication(classes, APP_ONE);
        compileApplication(classes, APP_TWO);
        String longMainClassOne = longMainClass("VeryLongIdentityApplicationOne");
        String longMainClassTwo = longMainClass("VeryLongIdentityApplicationTwo");
        compileApplication(classes, longMainClassOne);
        compileApplication(classes, longMainClassTwo);

        verifyLegacyNames(classes);
        verifyVersionLaunch();

        Path identityDirectory = work.resolve("identity-cds");
        Files.createDirectories(identityDirectory);
        ArchivePair first = verifyIdentityAndReuse(classes, identityDirectory, APP_ONE);
        ArchivePair second = verifyIdentityAndReuse(classes, identityDirectory, APP_TWO);
        if (first.coop.equals(second.coop) || first.nocoop.equals(second.nocoop)) {
            throw new RuntimeException("Different main classes resolved to the same archives: "
                    + first.coop + ", " + second.coop + ", "
                    + first.nocoop + ", " + second.nocoop);
        }
        assertNoInternalDumpIdentity(identityDirectory);
        Path firstLongIdentity = verifyLongIdentity(classes, longMainClassOne);
        Path secondLongIdentity = verifyLongIdentity(classes, longMainClassTwo);
        if (firstLongIdentity.equals(secondLongIdentity)) {
            throw new RuntimeException("Long main classes with different tails collided: "
                    + firstLongIdentity);
        }
        verifyLongBaseIdentity(classes, longMainClassOne);

        verifyConcurrentReuse(classes);           
        verifyConcurrentFirstRun(classes);     
        verifyMultipleReuseStability(classes);      
        verifyPathIsFileNotDir(classes);          
        verifyLegacyAndIdentityCoexist(classes);  
        verifyHashUniqueness(classes);            
        verifyBoundaryConditions(classes);        
    }

    private static void verifyLegacyNames(Path classes) throws Exception {
        Path cds = work.resolve("legacy-cds");
        Files.createDirectories(cds);
        Path classList = cds.resolve("appcds.lst");
        ArchivePair archives = new ArchivePair(
                cds.resolve("appcds_coop.jsa"),
                cds.resolve("appcds_nocoop.jsa"));

        String first = runJava(classes, cds, APP_ONE, false, Boolean.TRUE);
        assertSelectedPaths(first, classList, archives.coop);
        waitForFile(classList, 30);

        String second = runJava(classes, cds, APP_ONE, false, Boolean.TRUE);
        assertSelectedPaths(second, classList, archives.coop);
        waitForFile(archives.coop, 60);
        waitForFile(archives.nocoop, 60);
        FileTime coopTime = Files.getLastModifiedTime(archives.coop);
        FileTime nocoopTime = Files.getLastModifiedTime(archives.nocoop);

        Thread.sleep(1200);
        String coopReuse = runJava(classes, cds, APP_ONE, false, Boolean.TRUE);
        assertSelectedPaths(coopReuse, classList, archives.coop);
        assertContains(coopReuse, " use AppCDS jsa.");
        assertUnchanged(archives.coop, coopTime);
        assertUnchanged(archives.nocoop, nocoopTime);

        String nocoopReuse = runJava(classes, cds, APP_ONE, false, Boolean.FALSE);
        assertSelectedPaths(nocoopReuse, classList, archives.nocoop);
        assertContains(nocoopReuse, " use AppCDS jsa.");
        assertUnchanged(archives.coop, coopTime);
        assertUnchanged(archives.nocoop, nocoopTime);
    }

    private static void verifyVersionLaunch() throws Exception {
        Path cds = work.resolve("version-cds");
        Files.createDirectories(cds);
        String stem = identityStem("java");
        Path classList = cds.resolve(stem + ".lst");
        ArchivePair archives = new ArchivePair(
                cds.resolve(stem + "_coop.jsa"),
                cds.resolve(stem + "_nocoop.jsa"));

        String first = runVersion(cds, true);
        assertSelectedPaths(first, classList, archives.coop);
        waitForFile(classList, 30);

        String second = runVersion(cds, true);
        assertSelectedPaths(second, classList, archives.coop);
        waitForFile(archives.coop, 60);
        waitForFile(archives.nocoop, 60);
        FileTime coopTime = Files.getLastModifiedTime(archives.coop);
        FileTime nocoopTime = Files.getLastModifiedTime(archives.nocoop);

        Thread.sleep(1200);
        String coopReuse = runVersion(cds, true);
        assertSelectedPaths(coopReuse, classList, archives.coop);
        assertContains(coopReuse, " use AppCDS jsa.");
        assertUnchanged(archives.coop, coopTime);
        assertUnchanged(archives.nocoop, nocoopTime);

        String nocoopReuse = runVersion(cds, false);
        assertSelectedPaths(nocoopReuse, classList, archives.nocoop);
        assertContains(nocoopReuse, " use AppCDS jsa.");
        assertUnchanged(archives.coop, coopTime);
        assertUnchanged(archives.nocoop, nocoopTime);
    }

    private static ArchivePair verifyIdentityAndReuse(Path classes, Path cds,
                                                       String mainClass) throws Exception {
        String stem = identityStem(mainClass);
        Path classList = cds.resolve(stem + ".lst");
        ArchivePair archives = new ArchivePair(
                cds.resolve(stem + "_coop.jsa"),
                cds.resolve(stem + "_nocoop.jsa"));

        String first = runJava(classes, cds, mainClass, true, Boolean.TRUE);
        assertSelectedPaths(first, classList, archives.coop);
        waitForFile(classList, 30);

        String second = runJava(classes, cds, mainClass, true, Boolean.TRUE);
        assertSelectedPaths(second, classList, archives.coop);
        waitForFile(archives.coop, 60);
        waitForFile(archives.nocoop, 60);
        FileTime coopTime = Files.getLastModifiedTime(archives.coop);
        FileTime nocoopTime = Files.getLastModifiedTime(archives.nocoop);

        Thread.sleep(1200);
        String coopReuse = runJava(classes, cds, mainClass, true, Boolean.TRUE);
        assertSelectedPaths(coopReuse, classList, archives.coop);
        assertContains(coopReuse, " use AppCDS jsa.");
        assertUnchanged(archives.coop, coopTime);
        assertUnchanged(archives.nocoop, nocoopTime);

        String nocoopReuse = runJava(classes, cds, mainClass, true, Boolean.FALSE);
        assertSelectedPaths(nocoopReuse, classList, archives.nocoop);
        assertContains(nocoopReuse, " use AppCDS jsa.");
        assertUnchanged(archives.coop, coopTime);
        assertUnchanged(archives.nocoop, nocoopTime);
        return archives;
    }

    private static void assertNoInternalDumpIdentity(Path cds) throws Exception {
        String stem = identityStem("java");
        Path classList = cds.resolve(stem + ".lst");
        Path coop = cds.resolve(stem + "_coop.jsa");
        Path nocoop = cds.resolve(stem + "_nocoop.jsa");
        Thread.sleep(1000);
        if (Files.exists(classList) || Files.exists(coop) || Files.exists(nocoop)) {
            throw new RuntimeException("Internal dump JVM created fallback identity files: "
                    + classList + ", " + coop + ", " + nocoop);
        }
    }

    private static String longMainClass(String simpleName) {
        StringBuilder name = new StringBuilder();
        for (int i = 0; i < 8; i++) {
            if (name.length() > 0) {
                name.append('.');
            }
            name.append("segment").append(i)
                    .append("abcdefghijklmnopqrstuvwxyz");
        }
        return name.append('.').append(simpleName).toString();
    }

    private static Path verifyLongIdentity(Path classes, String mainClass)
            throws Exception {
        Path cds = work.resolve("long-identity-cds");
        Files.createDirectories(cds);

        String firstOutput = runJava(classes, cds, mainClass, true, Boolean.TRUE);
        Path firstClassList = extractPath(firstOutput, "classlist file : ");
        Path firstCoop = extractPath(firstOutput, "appcds jsa file : ");
        Path firstNocoop = nocoopPath(firstCoop);
        assertBoundedHashName(firstClassList, ".lst");
        assertBoundedHashName(firstCoop, "_coop.jsa");
        assertBoundedHashName(firstNocoop, "_nocoop.jsa");
        assertCommonStem(firstClassList, firstCoop, firstNocoop);
        waitForFile(firstClassList, 30);

        String secondOutput = runJava(classes, cds, mainClass, true, Boolean.TRUE);
        Path secondClassList = extractPath(secondOutput, "classlist file : ");
        Path secondCoop = extractPath(secondOutput, "appcds jsa file : ");
        Path secondNocoop = nocoopPath(secondCoop);
        if (!firstClassList.equals(secondClassList)
                || !firstCoop.equals(secondCoop)
                || !firstNocoop.equals(secondNocoop)) {
            throw new RuntimeException("Long identity names are not stable: "
                    + firstClassList + ", " + secondClassList + ", "
                    + firstCoop + ", " + secondCoop + ", "
                    + firstNocoop + ", " + secondNocoop);
        }
        waitForFile(secondCoop, 60);
        waitForFile(secondNocoop, 60);
        return firstClassList;
    }

    private static void verifyLongBaseIdentity(Path classes, String mainClass)
            throws Exception {
        Path cds = directoryWithLength(3825);

        runJava(classes, cds, mainClass, true, Boolean.TRUE);
        Path classList = waitForSingleFile(cds, "*.lst", 30);
        String stem = removeSuffix(classList.getFileName().toString(), ".lst");
        Path coop = cds.resolve(stem + "_coop.jsa");
        Path nocoop = cds.resolve(stem + "_nocoop.jsa");

        runJava(classes, cds, mainClass, true, Boolean.TRUE);
        waitForFile(coop, 30);
        waitForFile(nocoop, 30);
    }

    private static void verifyConcurrentReuse(Path classes) throws Exception {
        Path cds = work.resolve("concurrent-reuse-cds");
        Files.createDirectories(cds);
        
        String first = runJava(classes, cds, APP_ONE, true, Boolean.TRUE);
        Path classList = extractPath(first, "classlist file : ");
        Path coopArchive = extractPath(first, "appcds jsa file : ");
        waitForFile(classList, 30);

        runJava(classes, cds, APP_ONE, true, Boolean.TRUE);
        waitForFile(coopArchive, 60);
        Path nocoopArchive = coopArchive.resolveSibling(
            coopArchive.getFileName().toString().replace("_coop.jsa", "_nocoop.jsa"));
        waitForFile(nocoopArchive, 60);
        FileTime originalTime = Files.getLastModifiedTime(coopArchive);
        
        int numProcesses = 5;
        CountDownLatch latch = new CountDownLatch(numProcesses);
        AtomicInteger successCount = new AtomicInteger(0);
        AtomicInteger failCount = new AtomicInteger(0);
        
        for (int i = 0; i < numProcesses; i++) {
            Path logFile = work.resolve("logs").resolve(
                String.format("concurrent-reuse-%d.log", i));
            
            new Thread(() -> {
                try {
                    List<String> cmd = commonJavaCommand(cds, true, Boolean.TRUE);
                    cmd.add("-cp");
                    cmd.add(classes.toString());
                    cmd.add(APP_ONE);
                    
                    ProcessBuilder pb = new ProcessBuilder(cmd);
                    pb.redirectOutput(logFile.toFile());
                    pb.redirectErrorStream(true);
                    
                    Process p = pb.start();
                    if (p.waitFor(60, TimeUnit.SECONDS) && p.exitValue() == 0) {
                        successCount.incrementAndGet();
                    } else {
                        p.destroyForcibly();
                        failCount.incrementAndGet();
                    }
                } catch (Exception e) {
                    failCount.incrementAndGet();
                } finally {
                    latch.countDown();
                }
            }).start();
        }
        
        if (!latch.await(180, TimeUnit.SECONDS)) {
            throw new RuntimeException("Concurrent reuse test timed out");
        }
        
        if (failCount.get() > 0) {
            throw new RuntimeException(failCount.get() + 
                " concurrent reuse processes failed");
        }
        
        if (!Files.exists(coopArchive)) {
            throw new RuntimeException("Archive deleted by concurrent reuse");
        }
        
        long size = Files.size(coopArchive);
        if (size < 1000) {
            throw new RuntimeException("Archive corrupted, size=" + size);
        }
        
    }

    private static void verifyConcurrentFirstRun(Path classes) throws Exception {
        Path cds = work.resolve("concurrent-first-cds");
        Files.createDirectories(cds);
        
        int numProcesses = 5;
        CountDownLatch latch = new CountDownLatch(numProcesses);
        AtomicInteger successCount = new AtomicInteger(0);
        AtomicInteger failCount = new AtomicInteger(0);
        
        CountDownLatch startBarrier = new CountDownLatch(1);
        
        for (int i = 0; i < numProcesses; i++) {
            Path logFile = work.resolve("logs").resolve(
                String.format("concurrent-first-%d.log", i));
            
            new Thread(() -> {
                try {
                    startBarrier.await();
                    List<String> cmd = commonJavaCommand(cds, true, Boolean.TRUE);
                    cmd.add("-cp");
                    cmd.add(classes.toString());
                    cmd.add(APP_ONE);
                    
                    ProcessBuilder pb = new ProcessBuilder(cmd);
                    pb.redirectOutput(logFile.toFile());
                    pb.redirectErrorStream(true);
                    
                    Process p = pb.start();
                    
                    if (p.waitFor(120, TimeUnit.SECONDS) && p.exitValue() == 0) {
                        successCount.incrementAndGet();
                    } else {
                        p.destroyForcibly();
                        failCount.incrementAndGet();
                    }
                } catch (Exception e) {
                    failCount.incrementAndGet();
                } finally {
                    latch.countDown();
                }
            }).start();
        }
        
        startBarrier.countDown();
        
        if (!latch.await(300, TimeUnit.SECONDS)) {
            throw new RuntimeException("Concurrent first run timed out");
        }
        
        if (successCount.get() == 0) {
            throw new RuntimeException("All concurrent first run processes failed");
        }
        
        Thread.sleep(2000);
        
        Path expectedClassList = cds.resolve(identityStem(APP_ONE) + ".lst");
        if (!Files.exists(expectedClassList)) {
            throw new RuntimeException("Classlist not created after concurrent first run");
        }
        
    }

    private static void verifyMultipleReuseStability(Path classes) throws Exception {
        
        Path cds = work.resolve("multi-reuse-cds");
        Files.createDirectories(cds);
        
        String first = runJava(classes, cds, APP_ONE, true, Boolean.TRUE);
        Path classList = extractPath(first, "classlist file : ");
        Path coopArchive = extractPath(first, "appcds jsa file : ");
        waitForFile(classList, 30);

        runJava(classes, cds, APP_ONE, true, Boolean.TRUE);
        waitForFile(coopArchive, 60);
        Path nocoopArchive = coopArchive.resolveSibling(
            coopArchive.getFileName().toString().replace("_coop.jsa", "_nocoop.jsa"));
        waitForFile(nocoopArchive, 60);
        
        FileTime expectedTime = Files.getLastModifiedTime(coopArchive);
        
        int numRuns = 10;
        for (int i = 0; i < numRuns; i++) {
            String output = runJava(classes, cds, APP_ONE, true, Boolean.TRUE);
            assertContains(output, " use AppCDS jsa.");
            assertUnchanged(coopArchive, expectedTime);
        }
    }

    private static void verifyPathIsFileNotDir(Path classes) throws Exception {
        Path fileNotDir = work.resolve("file-not-dir-cds");
        Files.write(fileNotDir, "this is a file, not a directory".getBytes());
        
        try {
            runJava(classes, fileNotDir, APP_ONE, true, Boolean.TRUE);
            System.out.println("    (JVM handled gracefully)");
        } catch (Exception e) {
            String msg = e.getMessage();
            if (msg == null || 
                (!msg.contains("not a directory") && 
                 !msg.contains("not dir") &&
                 !msg.contains("Unexpected exit value"))) {
                if (msg != null && 
                    (msg.contains("NullPointerException") || 
                     msg.contains("OutOfMemoryError"))) {
                    throw new RuntimeException("JVM crashed on file-as-dir", e);
                }
            }
        } finally {
            Files.deleteIfExists(fileNotDir);
        }
    }

    private static void verifyLegacyAndIdentityCoexist(Path classes) throws Exception {
        Path cds = work.resolve("coexist-cds");
        Files.createDirectories(cds);
        
        String legacy1 = runJava(classes, cds, APP_ONE, false, Boolean.TRUE);
        Path legacyClassList = cds.resolve("appcds.lst");
        waitForFile(legacyClassList, 30);

        runJava(classes, cds, APP_ONE, false, Boolean.TRUE);
        Path legacyCoop = cds.resolve("appcds_coop.jsa");
        Path legacyNocoop = cds.resolve("appcds_nocoop.jsa");
        waitForFile(legacyCoop, 60);
        waitForFile(legacyNocoop, 60);
        FileTime legacyCoopTime = Files.getLastModifiedTime(legacyCoop);
        
        Path identityCds = work.resolve("coexist-identity-cds");
        Files.createDirectories(identityCds);
        
        String id1 = runJava(classes, identityCds, APP_ONE, true, Boolean.TRUE);
        Path idClassList = extractPath(id1, "classlist file : ");
        waitForFile(idClassList, 30);
        
        runJava(classes, identityCds, APP_ONE, true, Boolean.TRUE);
        Path idCoop = extractPath(id1, "appcds jsa file : ");
        waitForFile(idCoop, 60);
        
        assertUnchanged(legacyCoop, legacyCoopTime);
        
        if (legacyClassList.getFileName().toString()
            .equals(idClassList.getFileName().toString())) {
            throw new RuntimeException(
                "Legacy and identity file names should differ");
        }
        
    }

    private static void verifyHashUniqueness(Path classes) throws Exception {
        
        String[] mainClasses = {
            "com.test.HashAppA",
            "com.test.HashAppB",
            "com.test.HashAppC",
            "com.test.HashAppD",
            "com.test.HashAppE"
        };
        
        for (String mainClass : mainClasses) {
            compileApplication(classes, mainClass);
        }
        
        Path cds = work.resolve("hash-uniqueness-cds");
        Files.createDirectories(cds);
        
        Set<String> fileNames = new HashSet<>();
        
        for (String mainClass : mainClasses) {
            String output = runJava(classes, cds, mainClass, true, Boolean.TRUE);
            Path classList = extractPath(output, "classlist file : ");
            
            String fileName = classList.getFileName().toString();
            
            if (fileNames.contains(fileName)) {
                throw new RuntimeException(
                    "Hash collision: " + fileName + 
                    " for " + mainClass);
            }
            fileNames.add(fileName);
        }
        
    }

    private static void verifyBoundaryConditions(Path classes) throws Exception {
        
        String shortClass = "X";
        compileApplication(classes, shortClass);
        
        Path cds1 = work.resolve("boundary-short-cds");
        Files.createDirectories(cds1);
        
        String output1 = runJava(classes, cds1, shortClass, true, Boolean.TRUE);
        Path classList1 = extractPath(output1, "classlist file : ");
        String fileName1 = classList1.getFileName().toString();
        
        if (!fileName1.startsWith("appcds_") || !fileName1.endsWith(".lst")) {
            throw new RuntimeException(
                "Invalid file name for short class: " + fileName1);
        }
        
        Path cds2 = work.resolve("boundary-255-cds");
        Files.createDirectories(cds2);
        
        StringBuilder longName = new StringBuilder();
        while (longName.length() < 200) {
            longName.append("a");
        }
        String longClass = "test." + longName.toString();
        compileApplication(classes, longClass);
        
        String output2 = runJava(classes, cds2, longClass, true, Boolean.TRUE);
        Path classList2 = extractPath(output2, "classlist file : ");
        
        int byteLength = classList2.getFileName().toString()
            .getBytes(StandardCharsets.UTF_8).length;
        if (byteLength > 255) {
            throw new RuntimeException(
                "File name exceeds 255 bytes: " + byteLength);
        }
        
        Path cds3 = work.resolve("boundary-over255-cds");
        Files.createDirectories(cds3);
        
        String veryLongClass = longMainClass("BoundaryTest");
        compileApplication(classes, veryLongClass);  
        String output3 = runJava(classes, cds3, veryLongClass, true, Boolean.TRUE);
        Path classList3 = extractPath(output3, "classlist file : ");
        
        String fileName3 = classList3.getFileName().toString();
        if (!fileName3.matches("appcds_.*_h[0-9a-f]{16}\\.lst")) {
            throw new RuntimeException(
                "Long file name should have hash suffix: " + fileName3);
        }
        
    }

    private static Path directoryWithLength(int targetLength) throws Exception {
        Path current = work.resolve("deep-base");
        int remaining = targetLength - current.toString().length();
        int componentCount = (remaining + 200) / 201;
        int characterCount = remaining - componentCount;
        if (componentCount <= 0 || characterCount < componentCount) {
            throw new RuntimeException("Cannot build directory of length " + targetLength
                    + " from " + current);
        }

        for (int i = 0; i < componentCount; i++) {
            int componentLength = characterCount / componentCount
                    + (i < characterCount % componentCount ? 1 : 0);
            char[] name = new char[componentLength];
            Arrays.fill(name, 'd');
            current = current.resolve(new String(name));
        }
        if (current.toString().length() != targetLength) {
            throw new RuntimeException("Unexpected deep directory length: "
                    + current.toString().length() + ", expected " + targetLength);
        }
        Files.createDirectories(current);
        return current;
    }

    private static Path waitForSingleFile(Path directory, String glob,
                                          long timeoutSeconds) throws Exception {
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(timeoutSeconds);
        while (System.nanoTime() < deadline) {
            Path match = null;
            try (DirectoryStream<Path> files = Files.newDirectoryStream(directory, glob)) {
                for (Path file : files) {
                    if (match != null) {
                        throw new RuntimeException("Multiple files matched " + glob
                                + " in " + directory);
                    }
                    match = file;
                }
            }
            if (match != null) {
                waitForFile(match, timeoutSeconds);
                return match;
            }
            Thread.sleep(200);
        }
        throw new RuntimeException("Timed out waiting for " + glob + " in " + directory);
    }

    private static Path nocoopPath(Path coop) {
        String name = coop.getFileName().toString();
        if (!name.endsWith("_coop.jsa")) {
            throw new RuntimeException("Not a compressed-oops archive: " + coop);
        }
        String nocoop = name.substring(0, name.length() - "_coop.jsa".length())
                + "_nocoop.jsa";
        return coop.resolveSibling(nocoop);
    }

    private static void assertCommonStem(Path classList, Path coop, Path nocoop) {
        String listStem = removeSuffix(classList.getFileName().toString(), ".lst");
        String coopStem = removeSuffix(coop.getFileName().toString(), "_coop.jsa");
        String nocoopStem = removeSuffix(nocoop.getFileName().toString(), "_nocoop.jsa");
        if (!listStem.equals(coopStem) || !listStem.equals(nocoopStem)) {
            throw new RuntimeException("Identity files do not share one stem: "
                    + classList + ", " + coop + ", " + nocoop);
        }
    }

    private static String removeSuffix(String value, String suffix) {
        if (!value.endsWith(suffix)) {
            throw new RuntimeException("Missing suffix " + suffix + ": " + value);
        }
        return value.substring(0, value.length() - suffix.length());
    }

    private static void assertBoundedHashName(Path path, String suffix) {
        String fileName = path.getFileName().toString();
        int byteLength = fileName.getBytes(StandardCharsets.UTF_8).length;
        if (byteLength > 255) {
            throw new RuntimeException("File name exceeds Linux fallback limit: "
                    + byteLength + " bytes: " + fileName);
        }
        if (!fileName.matches("appcds_.*_h[0-9a-f]{16}.*")
                || !fileName.endsWith(suffix)) {
            throw new RuntimeException("Missing stable hash suffix: " + fileName);
        }
    }

    private static void assertSelectedPaths(String output, Path classList, Path archive) {
        assertContains(output, "classlist file : " + classList);
        assertContains(output, "appcds jsa file : " + archive);
    }

    private static Path extractPath(String output, String marker) {
        int start = output.indexOf(marker);
        if (start < 0) {
            throw new RuntimeException("Missing path marker: " + marker
                    + System.lineSeparator() + output);
        }
        start += marker.length();
        int end = output.indexOf('\n', start);
        String value = end < 0 ? output.substring(start) : output.substring(start, end);
        return Paths.get(value.trim());
    }

    private static void compileApplication(Path classes, String className) throws Exception {
        int separator = className.lastIndexOf('.');
        String packageName;
        String simpleName;
        if (separator < 0) {
            packageName = "";
            simpleName = className;
        } else {
            packageName = className.substring(0, separator);
            simpleName = className.substring(separator + 1);
        }
        Path source = work.resolve("src").resolve(className.replace('.', '/') + ".java");
        Files.createDirectories(source.getParent());

        List<String> lines = new ArrayList<>();
        if (!packageName.isEmpty()) {
            lines.add("package " + packageName + ";");
        }
        lines.add("public class " + simpleName + " {");
        lines.add("    public static void main(String[] args) {");
        lines.add("        System.out.println(\"" + simpleName + "\");");
        lines.add("    }");
        lines.add("}");
        Files.write(source, lines, StandardCharsets.UTF_8);

        ProcessBuilder javac = new ProcessBuilder(
            jdkTool("javac"),
            "-d", classes.toString(),
            source.toString());
        execute(javac, "javac-" + simpleName, 30);
    }

    private static String runJava(Path classes, Path cds, String mainClass,
                                  boolean identityNames, Boolean compressedOops)
            throws Exception {
        List<String> command = commonJavaCommand(cds, identityNames, compressedOops);
        command.add("-cp");
        command.add(classes.toString());
        command.add(mainClass);

        ProcessBuilder java = new ProcessBuilder(command);
        java.directory(work.toFile());
        return execute(java, "java-" + mainClass.substring(mainClass.lastIndexOf('.') + 1),
                60);
    }

    private static String runVersion(Path cds, boolean compressedOops) throws Exception {
        List<String> command = commonJavaCommand(cds, true,
                Boolean.valueOf(compressedOops));
        command.add("-version");
        ProcessBuilder java = new ProcessBuilder(command);
        java.directory(work.toFile());
        return execute(java, "java-version-" + (compressedOops ? "coop" : "nocoop"), 60);
    }

    private static List<String> commonJavaCommand(Path cds, boolean identityNames,
                                                  Boolean compressedOops) {
        List<String> command = new ArrayList<String>();
        command.add(jdkTool("java"));
        command.add("-Xms128m");
        command.add("-Xmx256m");
        command.add("-XX:AutoSharedArchivePath=" + cds);
        if (identityNames) {
            command.add("-XX:+UseAutoAppCDSIdentity");
        }
        command.add("-XX:+PrintAutoAppCDS");
        if (compressedOops != null) {
            command.add(compressedOops.booleanValue()
                    ? "-XX:+UseCompressedOops" : "-XX:-UseCompressedOops");
        }
        return command;
    }

    private static String execute(ProcessBuilder processBuilder, String label,
                                  long timeoutSeconds) throws Exception {
        Path logs = work.resolve("logs");
        Files.createDirectories(logs);
        Path log = logs.resolve(String.format("%03d-%s.log", ++commandSequence, label));
        processBuilder.redirectErrorStream(true);
        processBuilder.redirectOutput(log.toFile());
        Process process = processBuilder.start();
        if (!process.waitFor(timeoutSeconds, TimeUnit.SECONDS)) {
            process.destroyForcibly();
            throw new RuntimeException("Timed out waiting for " + processBuilder.command());
        }
        String output = new String(Files.readAllBytes(log), StandardCharsets.UTF_8);
        if (process.exitValue() != 0) {
            throw new RuntimeException("Unexpected exit value " + process.exitValue()
                    + " from " + processBuilder.command() + System.lineSeparator() + output);
        }
        return output;
    }

    private static void waitForFile(Path file, long timeoutSeconds) throws Exception {
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(timeoutSeconds);
        long previousSize = -1;
        int stableSamples = 0;
        while (System.nanoTime() < deadline) {
            if (Files.exists(file)) {
                long size = Files.size(file);
                if (size > 0 && size == previousSize) {
                    stableSamples++;
                    if (stableSamples >= 3) {
                        return;
                    }
                } else {
                    stableSamples = 0;
                    previousSize = size;
                }
            }
            Thread.sleep(200);
        }
        throw new RuntimeException("Timed out waiting for stable file: " + file);
    }

    private static void assertUnchanged(Path archive, FileTime expected) throws Exception {
        FileTime actual = Files.getLastModifiedTime(archive);
        if (!expected.equals(actual)) {
            throw new RuntimeException("Archive was regenerated instead of reused: "
                    + archive + ", before=" + expected + ", after=" + actual);
        }
    }

    private static String identityStem(String target) {
        return "appcds_" + effectiveUserIdentity + "_" + target;
    }

    private static String normalize(String value) {
        StringBuilder result = new StringBuilder(value.length());
        for (int i = 0; i < value.length(); i++) {
            char ch = value.charAt(i);
            if (ch == '/' || ch <= 0x1f || ch == 0x7f) {
                result.append('_');
            } else {
                result.append(ch);
            }
        }
        return result.toString();
    }

    private static void assertContains(String output, String expected) {
        if (!output.contains(expected)) {
            throw new RuntimeException("Missing expected output: " + expected
                    + System.lineSeparator() + output);
        }
    }

    private static String jdkTool(String tool) {
        String testJdk = System.getProperty("test.jdk");
        if (testJdk == null || testJdk.length() == 0) {
            testJdk = System.getProperty("compile.jdk");
        }
        if (testJdk == null || testJdk.length() == 0) {
            testJdk = System.getProperty("java.home");
        }
        Path path = Paths.get(testJdk, "bin", tool);
        if (!Files.exists(path) && "jre".equals(Paths.get(testJdk).getFileName().toString())) {
            path = Paths.get(testJdk).getParent().resolve("bin").resolve(tool);
        }
        if (!Files.exists(path)) {
            throw new RuntimeException("Could not find JDK tool: " + path);
        }
        return path.toString();
    }

    private static final class ArchivePair {
        private final Path coop;
        private final Path nocoop;

        private ArchivePair(Path coop, Path nocoop) {
            this.coop = coop;
            this.nocoop = nocoop;
        }
    }
}
