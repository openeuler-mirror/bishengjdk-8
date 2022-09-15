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

import org.openeuler.security.openssl.KAEProvider;

import java.io.File;
import java.util.ArrayList;
import java.util.List;

/*
 * @test
 * @summary Test KAE log
 * @requires os.arch=="aarch64"
 * @run main/othervm KAELogTest
 * @run main/othervm -Dkae.log=false KAELogTest
 * @run main/othervm -Dkae.log=true KAELogTest
 * @run main/othervm -Dkae.log=true -Dkae.log.file=./KAELogTest/kae.log KAELogTest
 */
public class KAELogTest {
    private static final String DEFAULT_LOG_PATH = System.getProperty("user.dir") +
            File.separator + "kae.log";

    private static final String SPECIFY_LOG_PATH = System.getProperty("user.dir") +
            File.separator + "KAELogTest" + File.separator + "kae.log";

    private static final List<File> files = new ArrayList<>();

    enum Mode {
        DEFAULT,
        DISABLE,
        ENABLE,
        SPECIFY
    }

    public static void main(String[] args) {
        Mode mode = getMode();
        try {
            new KAEProvider();
            test(mode);
        } finally {
            KAETestHelper.cleanUp(files);
        }
    }

    private static Mode getMode() {
        String enableKaeLog = System.getProperty("kae.log");
        if (enableKaeLog == null) {
            return Mode.DEFAULT;
        } else if ("false".equals(enableKaeLog)) {
            return Mode.DISABLE;
        } else {
            String logPath = System.getProperty("kae.log.file");
            if (logPath == null) {
                return Mode.ENABLE;
            }
            return Mode.SPECIFY;
        }
    }

    private static void testDefault() {
        testDisable();
    }

    private static void testDisable() {
        File file = new File(DEFAULT_LOG_PATH);
        if (file.exists()) {
            throw new RuntimeException("test failed");
        }
    }

    private static void testEnable() {
        File file = new File(DEFAULT_LOG_PATH);
        if (!file.exists()) {
            throw new RuntimeException("test failed");
        }
        files.add(file);
    }

    private static void testSpecify() {
        File file = new File(KAELogTest.SPECIFY_LOG_PATH);
        if (!file.exists()) {
            throw new RuntimeException("test failed");
        }
        files.add(file);
        files.add(file.getParentFile());
    }

    private static void test(Mode mode) {
        switch (mode) {
            case DEFAULT:
                testDefault();
                break;
            case DISABLE:
                testDisable();
                break;
            case ENABLE:
                testEnable();
                break;
            case SPECIFY:
                testSpecify();
                break;
            default:
                throw new IllegalArgumentException("invalid mode");
        }
    }
}
