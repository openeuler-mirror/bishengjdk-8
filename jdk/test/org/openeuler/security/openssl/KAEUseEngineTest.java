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
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/*
 * @test
 * @summary Test KAE property kae.<algorithm>.useKaeEngine
 * @requires os.arch=="aarch64"
 * @run main/othervm -Dkae.log=true -Dall.test=default KAEUseEngineTest
 * @run main/othervm -Dkae.log=true -Dkae.digest.useKaeEngine=true KAEUseEngineTest
 * @run main/othervm -Dkae.log=true -Dkae.aes.useKaeEngine=true KAEUseEngineTest
 * @run main/othervm -Dkae.log=true -Dkae.sm4.useKaeEngine=true KAEUseEngineTest
 * @run main/othervm -Dkae.log=true -Dkae.hmac.useKaeEngine=true KAEUseEngineTest
 * @run main/othervm -Dkae.log=true -Dkae.rsa.useKaeEngine=true KAEUseEngineTest
 * @run main/othervm -Dkae.log=true -Dkae.dh.useKaeEngine=true KAEUseEngineTest
 * @run main/othervm -Dkae.log=true -Dkae.ec.useKaeEngine=true KAEUseEngineTest
 * @run main/othervm -Dkae.log=true -Dall.test=enable -Dkae.digest.useKaeEngine=true -Dkae.aes.useKaeEngine=true -Dkae.sm4.useKaeEngine=true  -Dkae.hmac.useKaeEngine=true -Dkae.rsa.useKaeEngine=true -Dkae.dh.useKaeEngine=true -Dkae.ec.useKaeEngine=true   KAEUseEngineTest
 * @run main/othervm -Dkae.log=true -Dkae.digest.useKaeEngine=false KAEUseEngineTest
 * @run main/othervm -Dkae.log=true -Dkae.aes.useKaeEngine=true KAEUseEngineTest
 * @run main/othervm -Dkae.log=true -Dkae.sm4.useKaeEngine=true KAEUseEngineTest
 * @run main/othervm -Dkae.log=true -Dkae.hmac.useKaeEngine=true KAEUseEngineTest
 * @run main/othervm -Dkae.log=true -Dkae.rsa.useKaeEngine=true KAEUseEngineTest
 * @run main/othervm -Dkae.log=true -Dkae.dh.useKaeEngine=true KAEUseEngineTest
 * @run main/othervm -Dkae.log=true -Dkae.ec.useKaeEngine=true KAEUseEngineTest
 * @run main/othervm -Dkae.log=true -Dall.test=disable -Dkae.digest.useKaeEngine=false -Dkae.aes.useKaeEngine=false -Dkae.sm4.useKaeEngine=false  -Dkae.hmac.useKaeEngine=false -Dkae.rsa.useKaeEngine=false -Dkae.dh.useKaeEngine=false -Dkae.ec.useKaeEngine=false  KAEUseEngineTest
 * @run main/othervm -Dkae.log=true -Dall.test=default -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true KAEUseEngineTest
 * @run main/othervm -Dkae.log=true -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.digest.useKaeEngine=true KAEUseEngineTest
 * @run main/othervm -Dkae.log=true -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.aes.useKaeEngine=true KAEUseEngineTest
 * @run main/othervm -Dkae.log=true -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.sm4.useKaeEngine=true KAEUseEngineTest
 * @run main/othervm -Dkae.log=true -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.hmac.useKaeEngine=true KAEUseEngineTest
 * @run main/othervm -Dkae.log=true -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.rsa.useKaeEngine=true KAEUseEngineTest
 * @run main/othervm -Dkae.log=true -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.dh.useKaeEngine=true KAEUseEngineTest
 * @run main/othervm -Dkae.log=true -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.ec.useKaeEngine=true KAEUseEngineTest
 * @run main/othervm -Dkae.log=true -Dall.test=enable -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.digest.useKaeEngine=true -Dkae.aes.useKaeEngine=true -Dkae.sm4.useKaeEngine=true  -Dkae.hmac.useKaeEngine=true -Dkae.rsa.useKaeEngine=true -Dkae.dh.useKaeEngine=true -Dkae.ec.useKaeEngine=true   KAEUseEngineTest
 * @run main/othervm -Dkae.log=true -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.digest.useKaeEngine=false KAEUseEngineTest
 * @run main/othervm -Dkae.log=true -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.aes.useKaeEngine=true KAEUseEngineTest
 * @run main/othervm -Dkae.log=true -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.sm4.useKaeEngine=true KAEUseEngineTest
 * @run main/othervm -Dkae.log=true -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.hmac.useKaeEngine=true KAEUseEngineTest
 * @run main/othervm -Dkae.log=true -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.rsa.useKaeEngine=true KAEUseEngineTest
 * @run main/othervm -Dkae.log=true -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.dh.useKaeEngine=true KAEUseEngineTest
 * @run main/othervm -Dkae.log=true -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.ec.useKaeEngine=true KAEUseEngineTest
 * @run main/othervm -Dkae.log=true -Dall.test=disable -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.digest.useKaeEngine=false -Dkae.aes.useKaeEngine=false -Dkae.sm4.useKaeEngine=false  -Dkae.hmac.useKaeEngine=false -Dkae.rsa.useKaeEngine=false -Dkae.dh.useKaeEngine=false -Dkae.ec.useKaeEngine=false  KAEUseEngineTest
 */
public class KAEUseEngineTest {
    enum Mode {
        DEFAULT(new boolean[]{
                true, false, false, true, false, false, false, false, false, false,
                false, false, false, false, false, false, true, true, true, true,
                false, false, false, false, false, false, true, true, false
        }),
        DIGEST_ENABLE(new boolean[]{
                true, false, false, true, false, false, false, false, false, false,
                false, false, false, false, false, false, true, true, true, true,
                false, false, false, false, false, false, true, true, false
        }, 0, true),
        AES_ENABLE(new boolean[]{
                true, false, false, true, true, true, true, true, true, true,
                true, true, true, true, true, true, true, true, true, true,
                false, false, false, false, false, false, true, true, false
        }, 1, true),
        SM4_ENABLE(new boolean[]{
                true, false, false, true, false, false, false, false, false, false,
                false, false, false, false, false, false, true, true, true, true,
                false, false, false, false, false, false, true, true, false
        }, 2, true),
        HMAC_ENABLE(new boolean[]{
                true, false, false, true, false, false, false, false, false, false,
                false, false, false, false, false, false, true, true, true, true,
                true, true, true, true, true, true, true, true, false
        }, 3, true),
        RSA_ENABLE(new boolean[]{
                true, false, false, true, false, false, false, false, false, false,
                false, false, false, false, false, false, true, true, true, true,
                false, false, false, false, false, false, true, true, false
        }, 4, true),
        DH_ENABLE(new boolean[]{
                true, false, false, true, false, false, false, false, false, false,
                false, false, false, false, false, false, true, true, true, true,
                false, false, false, false, false, false, true, true, false
        }, 5, true),
        EC_ENABLE(new boolean[]{
                true, false, false, true, false, false, false, false, false, false,
                false, false, false, false, false, false, true, true, true, true,
                false, false, false, false, false, false, true, true, false
        }, 6, true),
        ALL_ENABLE(new boolean[]{
                true, false, false, true, true, true, true, true, true, true,
                true, true, true, true, true, true, true, true, true, true,
                true, true, true, true, true, true, true, true, false
        }, true),
        DIGEST_DISABLE(new boolean[]{
                false, false, false, false, false, false, false, false, false, false,
                false, false, false, false, false, false, true, true, true, true,
                false, false, false, false, false, false, true, true, false
        }, 0, false),
        AES_DISABLE(new boolean[]{
                true, false, false, true, false, false, false, false, false, false,
                false, false, false, false, false, false, true, true, true, true,
                false, false, false, false, false, false, true, true, false
        }, 1, false),
        SM4_DISABLE(new boolean[]{
                true, false, false, true, false, false, false, false, false, false,
                false, false, false, false, false, false, false, false, false, false,
                false, false, false, false, false, false, true, true, false
        }, 2, false),
        HMAC_DISABLE(new boolean[]{
                true, false, false, true, false, false, false, false, false, false,
                false, false, false, false, false, false, true, true, true, true,
                false, false, false, false, false, false, true, true, false
        }, 3, false),
        RSA_DISABLE(new boolean[]{
                true, false, false, true, false, false, false, false, false, false,
                false, false, false, false, false, false, true, true, true, true,
                false, false, false, false, false, false, false, true, false
        }, 4, false),
        DH_DISABLE(new boolean[]{
                true, false, false, true, false, false, false, false, false, false,
                false, false, false, false, false, false, true, true, true, true,
                false, false, false, false, false, false, true, false, false
        }, 5, false),
        EC_DISABLE(new boolean[]{
                true, false, false, true, false, false, false, false, false, false,
                false, false, false, false, false, false, true, true, true, true,
                false, false, false, false, false, false, true, true, false
        }, 6, false),
        ALL_DISABLE(new boolean[]{
                false, false, false, false, false, false, false, false, false, false,
                false, false, false, false, false, false, false, false, false, false,
                false, false, false, false, false, false, false, false, false
        }, false);
        private final boolean[] expectedResult;
        private final Integer propertyNameIndex;
        private final boolean enable;
        private static final Map<String, Mode> modeMap = new HashMap<>();

        static {
            Mode[] modes = Mode.values();
            for (Mode mode : modes) {
                if (mode.propertyNameIndex != null) {
                    modeMap.put(PROPERTY_NAMES[mode.propertyNameIndex] + ":" + mode.enable, mode);
                }
            }
            modeMap.put("default", DEFAULT);
            modeMap.put("disable", ALL_DISABLE);
            modeMap.put("enable", ALL_ENABLE);
        }

        Mode(boolean[] expectedResult) {
            this(expectedResult, false);
        }

        Mode(boolean[] expectedResult, boolean enable) {
            this(expectedResult, null, enable);
        }

        Mode(boolean[] expectedResult, Integer propertyNameIndex, boolean enable) {
            this.expectedResult = expectedResult;
            this.propertyNameIndex = propertyNameIndex;
            this.enable = enable;
        }

        static Mode getMode(String name, Boolean enable) {
            return modeMap.get(name + ":" + enable);
        }

        static Mode getMode(String key) {
            return modeMap.get(key);
        }
    }

    private static final String KAE_LOG_PATH = System.getProperty("user.dir") +
            File.separator + "kae.log";

    private static final String[] PROPERTY_NAMES = new String[]{
            "kae.digest.useKaeEngine",
            "kae.aes.useKaeEngine",
            "kae.sm4.useKaeEngine",
            "kae.hmac.useKaeEngine",
            "kae.rsa.useKaeEngine",
            "kae.dh.useKaeEngine",
            "kae.ec.useKaeEngine"
    };

    private static final List<File> files = new ArrayList<>();

    public static void main(String[] args) throws IOException {
        KAETestHelper.Engine engine = KAETestHelper.getEngine();
        if (!engine.isValid()) {
            System.out.println("Skip test, engine " + engine.getEngineId() + " does not exist.");
            return;
        }
        Mode mode = getMode();
        if (mode == null) {
            throw new RuntimeException("test failed: mode is null");
        }

        try {
            new KAEProvider();
            test(mode, engine);
        } finally {
            KAETestHelper.cleanUp(files);
        }
    }

    private static Mode getMode() {
        String value = System.getProperty("all.test");
        if (value != null) {
            return Mode.getMode(value);
        }
        for (String propertyName : PROPERTY_NAMES) {
            String property = System.getProperty(propertyName);
            Boolean enable = null;
            if (property != null) {
                enable = Boolean.valueOf(property);
            }
            Mode mode = Mode.getMode(propertyName, enable);
            if (mode != null) {
                return mode;
            }
        }
        return null;
    }

    private static void test(Mode mode, KAETestHelper.Engine engine) throws IOException {
        File file = new File(KAE_LOG_PATH);
        files.add(file);
        boolean[] kaeUseEngineFlags = KAETestHelper.parseLog(engine, file);
        if (!Arrays.equals(mode.expectedResult, kaeUseEngineFlags)) {
            throw new RuntimeException("test failed : expected : " + Arrays.toString(mode.expectedResult) + "," +
                    "actual:" + Arrays.toString(kaeUseEngineFlags));
        }
    }
}
