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

import org.openeuler.security.openssl.KAEConfig;
import org.openeuler.security.openssl.KAEProvider;

import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;

/*
 * @test
 * @summary Test KAE Conf
 * @requires os.arch=="aarch64"
 * @run main/othervm KAEConfTest DEFAULT
 * @run main/othervm KAEConfTest SPECIFY
 */
public class KAEConfTest {
    private static final String DEFAULT_CONF = System.getProperty("java.home") +
            File.separator + "lib" + File.separator + "kaeprovider.conf";

    private static final String SPECIFY_CONF = System.getProperty("user.dir") +
            File.separator + "kaeprovider.conf";

    private static final String SPECIFY_LOG_PATH = System.getProperty("user.dir") + File.separator + "kae.log";
    private static final List<File> files = new ArrayList<>();

    enum Mode {
        DEFAULT,
        SPECIFY
    }

    public static void main(String[] args) throws IOException {
        Mode mode = getMode(args);
        try {
            init(mode);
            new KAEProvider();
            test(mode);
        } finally {
            KAETestHelper.cleanUp(files);
        }
    }

    private static Mode getMode(String[] args) {
        if (args.length <= 0) {
            return Mode.DEFAULT;
        }
        return Mode.valueOf(args[0]);
    }

    private static void init(Mode mode) throws IOException {
        if (Mode.SPECIFY.equals(mode)) {
            System.setProperty("kae.conf", SPECIFY_CONF);
            File file = new File(SPECIFY_CONF);
            if (!file.exists()) {
                Files.createFile(file.toPath());
            }
            files.add(file);
            try (FileWriter fileWriter = new FileWriter(file)) {
                fileWriter.write("kae.log=true");
                fileWriter.flush();
            }
        }
    }

    private static void testDefault() {
        File file = new File(DEFAULT_CONF);
        if (!file.exists()) {
            throw new RuntimeException("test failed");
        }
    }

    private static void testSpecify() {
        String value = KAEConfig.privilegedGetOverridable("kae.log");
        if (!"true".equals(value)) {
            throw new RuntimeException("test failed : kae.log=" + value);
        }
        File file = new File(SPECIFY_LOG_PATH);
        if (!file.exists()) {
            throw new RuntimeException(SPECIFY_LOG_PATH + "does not exist");
        }
        // kae log file
        files.add(file);
    }

    private static void test(Mode mode) {
        switch (mode) {
            case DEFAULT:
                testDefault();
                break;
            case SPECIFY:
                testSpecify();
                break;
            default:
                throw new IllegalArgumentException("invalid mode");
        }
    }
}
