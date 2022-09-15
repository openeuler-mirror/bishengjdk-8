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

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.IOException;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

class KAETestHelper {
    private static final String KAE_ENGINE_ID = "kae";
    private static final String UADK_ENGINE_ID = "uadk_engine";
    private static boolean hasKaeEngine;
    private static boolean hasUadkEngine;

    private static String engineRootPath;

    // algorithm names
    private static final String[] ALGORITHM_NAMES = new String[]{
            "md5",
            "sha256",
            "sha384",
            "sm3",
            "aes-128-ecb",
            "aes-128-cbc",
            "aes-128-ctr",
            "aes-128-gcm",
            "aes-192-ecb",
            "aes-192-cbc",
            "aes-192-ctr",
            "aes-192-gcm",
            "aes-256-ecb",
            "aes-256-cbc",
            "aes-256-ctr",
            "aes-256-gcm",
            "sm4-ecb",
            "sm4-cbc",
            "sm4-ctr",
            "sm4-ofb",
            "hmac-md5",
            "hmac-sha1",
            "hmac-sha224",
            "hmac-sha256",
            "hmac-sha384",
            "hmac-sha512",
            "rsa",
            "dh",
            "ec"
    };
    private static final Map<String, Integer> ALGORITHM_NAME_MAP = new HashMap<>();

    private static final String PROVIDER_NAME = "KAEProvider";
    private static final String USE_OPENSSL_MSG = "Use openssl soft calculation";
    private static final String USE_KAE_HARDWARE_MSG = "enable KAE hardware acceleration";
    private static final Map<String, Boolean> ALGORITHM_MSG_MAP = new HashMap<>();

    static {
        init();
    }

    enum Engine {
        default_engine(hasKaeEngine, KAE_ENGINE_ID),
        kae(hasKaeEngine, KAE_ENGINE_ID),
        uadk_engine(hasUadkEngine, UADK_ENGINE_ID);
        private final boolean isValid;
        private final String engineId;

        Engine(boolean isValid, String engineId) {
            this.isValid = isValid;
            this.engineId = engineId;
        }

        public boolean isValid() {
            return isValid;
        }

        public String getEngineId() {
            return engineId;
        }
    }

    private static void init() {
        engineRootPath = System.getenv("OPENSSL_ENGINES");
        if (engineRootPath == null || engineRootPath.equals("")) {
            System.out.println("Environment variable OPENSSL_ENGINES is not configured");
        }
        hasKaeEngine = hasEngine(KAE_ENGINE_ID);
        hasUadkEngine = hasEngine(UADK_ENGINE_ID);

        for (int i = 0; i < ALGORITHM_NAMES.length; i++) {
            ALGORITHM_NAME_MAP.put(ALGORITHM_NAMES[i], i);
        }

        ALGORITHM_MSG_MAP.put(USE_OPENSSL_MSG, false);
        ALGORITHM_MSG_MAP.put(USE_KAE_HARDWARE_MSG, true);
    }

    static Integer getAlgorithmIndex(String algorithmName) {
        return ALGORITHM_NAME_MAP.get(algorithmName);
    }

    static String getAlgorithmName(Integer algorithmIndex) {
        return ALGORITHM_NAMES[algorithmIndex];
    }

    private static boolean hasEngine(String engineId) {
        String filePath = engineRootPath + File.separator + engineId + ".so";
        File file = new File(filePath);
        return file.exists();
    }

    static boolean hasKaeEngine() {
        return hasKaeEngine;
    }

    static boolean hasUadkEngine() {
        return hasUadkEngine;
    }

    static void cleanUp(List<File> files) {
        for (File file : files) {
            System.out.println("delete file : " + file);
            file.delete();
        }
    }

    static boolean[] parseLog(Engine engine, File file) throws IOException {
        boolean[] kaeUseEngineFlags;
        String expectedEngineMsg = engine.getEngineId() + " engine was found";
        try (BufferedReader reader = new BufferedReader(new FileReader(file))) {
            // load engine message
            String engineMsg = reader.readLine();
            if (engineMsg == null || !engineMsg.contains(expectedEngineMsg)) {
                throw new RuntimeException("test failed : actual message :" + engineMsg);
            }

            // summary message
            String summaryMessage = reader.readLine();
            if (summaryMessage == null) {
                throw new RuntimeException("test failed : summary message is null");
            }

            kaeUseEngineFlags = new boolean[ALGORITHM_NAMES.length];
            // strategy of each algorithm
            String strategy;
            while ((strategy = reader.readLine()) != null) {
                String[] splitArray = strategy.split("=>");
                if (splitArray.length < 2) {
                    throw new RuntimeException("test failed : strategy = " + strategy);
                }

                // algorithm Index
                String algorithm = splitArray[0].replace(" ", "");
                Integer algorithmIndex = ALGORITHM_NAME_MAP.get(algorithm);
                if (algorithmIndex == null) {
                    throw new RuntimeException("test failed : illegal algorithm " + algorithm);
                }

                // provider and algorithm value
                String detail = splitArray[1];
                String[] detailArray = detail.split(":");
                if (detailArray.length < 2) {
                    throw new RuntimeException("test failed : detail=" + strategy);
                }
                String provider = detailArray[0].replace(" ", "");
                if (!PROVIDER_NAME.equals(provider)) {
                    throw new RuntimeException("test failed : provider= " + provider);
                }
                String algorithmMsg = detailArray[1].trim();
                Boolean algorithmValue = ALGORITHM_MSG_MAP.get(algorithmMsg);
                if (algorithmValue == null) {
                    throw new RuntimeException("test failed : algorithmMsg= " + algorithmMsg);
                }
                kaeUseEngineFlags[algorithmIndex] = algorithmValue;
            }
        }
        return kaeUseEngineFlags;
    }

    static KAETestHelper.Engine getEngine() {
        String engineId = System.getProperty("kae.engine.id");
        if (engineId == null) {
            return KAETestHelper.Engine.default_engine;
        }
        return KAETestHelper.Engine.valueOf(engineId);
    }
}
