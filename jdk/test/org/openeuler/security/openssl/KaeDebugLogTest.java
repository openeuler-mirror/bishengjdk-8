/*
 * Copyright (c) 2021, Huawei Technologies Co., Ltd. All rights reserved.
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

import org.openeuler.security.openssl.KAEProvider;

import javax.crypto.Cipher;
import javax.crypto.spec.SecretKeySpec;
import java.io.PrintStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.security.Security;
import java.util.Objects;
import java.util.stream.Collectors;
import java.util.stream.Stream;

/**
 * @test
 * @summary test for KaeDebugLogTest
 * @requires os.arch=="aarch64"
 * @run main/othervm -Djava.security.debug=kae -Dkae.sm4.maxChunkSize=65536 KaeDebugLogTest
 * @run main/othervm -Djava.security.debug=kae KaeDebugLogTest
 * @run main/othervm -Djava.security.auth.debug=kae KaeDebugLogTest
 * @run main/othervm KaeDebugLogTest
 */

public class KaeDebugLogTest {

    private static final PrintStream err = System.err;

    public static void main(String[] args) throws Exception {
        PrintStream printStream = new PrintStream("kaetest.out");
        System.setErr(printStream);
        testDebugLog();
        System.setErr(printStream);
        testSm4ChunkSize();
    }

    public static void testDebugLog() throws Exception {
        new KAEProvider();
        Stream<String> lines = Files.lines(Paths.get("kaetest.out"));
        System.setErr(err);
        String content = lines.collect(Collectors.joining(System.lineSeparator()));
        if(("kae".equals(System.getProperty("java.security.debug"))
                || "kae".equals(System.getProperty("java.security..auth.debug")))
                && !content.contains("reading kae properties file:")){
            throw new RuntimeException("KaeDebugLogTest Failed! Failed to set the debug log.");
        }
        lines.close();
    }

    public static void testSm4ChunkSize() throws Exception {
        Security.insertProviderAt(new KAEProvider(), 1);
        Cipher cipher = Cipher.getInstance("SM4");
        cipher.init(Cipher.ENCRYPT_MODE, new SecretKeySpec("sm4EncryptionKey".getBytes(StandardCharsets.UTF_8), "SM4"));
        Stream<String> lines = Files.lines(Paths.get("kaetest.out"));
        System.setErr(err);
        String content = lines.collect(Collectors.joining(System.lineSeparator()));
        String log = "The configured chunk size is " + System.getProperty("kae.sm4.maxChunkSize");
        if(("kae".equals(System.getProperty("java.security.debug"))
                || "kae".equals(System.getProperty("java.security..auth.debug")))
                && Objects.nonNull(System.getProperty("kae.sm4.maxChunkSize")) &&!content.contains(log)){
            throw new RuntimeException("KaeDebugLogTest Failed! Failed to set the kae.sm4.maxChunkSize = " + System.getProperty("kae.sm4.maxChunkSize"));
        }
        lines.close();
    }

}
