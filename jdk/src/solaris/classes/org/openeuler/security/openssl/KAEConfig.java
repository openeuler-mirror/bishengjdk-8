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

package org.openeuler.security.openssl;

import sun.security.util.Debug;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.security.AccessController;
import java.security.PrivilegedAction;
import java.util.Arrays;
import java.util.HashMap;
import java.util.Map;
import java.util.Properties;

public class KAEConfig {
    private static final Debug kaeDebug = Debug.getInstance("kae");

    // these property names indicates whether each algorithm uses KAEProvider
    private static final String[] useKaeProviderPropertyNames = new String[]{
            "kae.md5",
            "kae.sha256",
            "kae.sha384",
            "kae.sm3",
            "kae.aes",
            "kae.sm4",
            "kae.hmac",
            "kae.rsa",
            "kae.dh",
            "kae.ec"
    };

    // these property names indicate whether KAE hardware acceleration is enabled for each algorithm
    private static final String[] useKaeEnginePropertyNames = new String[]{
            "kae.digest.useKaeEngine",
            "kae.aes.useKaeEngine",
            "kae.sm4.useKaeEngine",
            "kae.hmac.useKaeEngine",
            "kae.rsa.useKaeEngine",
            "kae.dh.useKaeEngine",
            "kae.ec.useKaeEngine"
    };

    // algorithm names
    private static final String[] algorithmNames = new String[]{
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

    // algorithm name and algorithm index mapping
    private static final Map<String, Integer> algorithmNameIndexMap = new HashMap<>();

    // algorithm name and algorithm category index mapping
    private static final Map<String, Integer> algorithmNameCategoryMap = new HashMap<>();

    // whether use KAEProvider for each algorithm
    private static final boolean[] useKaeProviderFlags = new boolean[algorithmNames.length];

    // whether use KAEProvider for each category algorithm
    private static final Map<String, Boolean> useKaeProviderCategoryMap = new HashMap<>();

    // whether enable the Kunpeng acceleration engine for each algorithm
    private static final boolean[] useKaeEngineFlags = new boolean[algorithmNames.length];

    // The kaeprovider.cnf properties
    private static Properties props;

    private KAEConfig() {

    }

    static {
        AccessController.doPrivileged(new PrivilegedAction<Void>() {
            public Void run() {
                initialize();
                return null;
            }
        });
    }

    private static File kaePropFile(String filename) {
        String sep = File.separator;
        String defaultKaeConf = System.getProperty("java.home") + sep + "lib" + sep + filename;
        String kaeConf = System.getProperty("kae.conf", defaultKaeConf);
        return new File(kaeConf);
    }

    private static void initialize() {
        initProperties();
        initAlgorithmNameMap();
        initUseKaeProviderFlags();
        initUseKaeEngineFlags();
    }

    private static void initProperties() {
        props = new Properties();
        File propFile = kaePropFile("kaeprovider.conf");
        if (propFile.exists()) {
            InputStream is = null;
            try {
                FileInputStream fis = new FileInputStream(propFile);
                is = new BufferedInputStream(fis);
                props.load(is);

                if (kaeDebug != null) {
                    kaeDebug.println("reading kae properties file: " +
                            propFile);
                }
            } catch (IOException e) {
                if (kaeDebug != null) {
                    kaeDebug.println("unable to load kae properties from " +
                            propFile);
                    e.printStackTrace();
                }
            } finally {
                if (is != null) {
                    try {
                        is.close();
                    } catch (IOException ioe) {
                        if (kaeDebug != null) {
                            kaeDebug.println("unable to close input stream");
                        }
                    }
                }
            }
        } else {
            if (kaeDebug != null) {
                kaeDebug.println("not found kae properties file: " +
                        propFile);
            }
        }
    }

    public static Boolean useKaeProvider(String key) {
        return useKaeProviderCategoryMap.getOrDefault(key, Boolean.TRUE);
    }

    private static void initUseKaeProviderFlags() {
        boolean[] categoryFlagsForProvider = new boolean[useKaeProviderPropertyNames.length];
        Arrays.fill(categoryFlagsForProvider, true);
        for (int i = 0; i < useKaeProviderPropertyNames.length; i++) {
            String configValue = privilegedGetOverridable(useKaeProviderPropertyNames[i]);
            if (configValue != null) {
                categoryFlagsForProvider[i] = Boolean.parseBoolean(configValue);
            }
            useKaeProviderCategoryMap.put(useKaeProviderPropertyNames[i], categoryFlagsForProvider[i]);
        }
        int offset = useKaeProviderPropertyNames.length - useKaeEnginePropertyNames.length;
        int digestAlgorithmLen = offset + 1;
        // digest
        System.arraycopy(categoryFlagsForProvider, 0, useKaeProviderFlags, 0, digestAlgorithmLen);

        // non-digest
        for (int i = digestAlgorithmLen; i < useKaeProviderFlags.length; i++) {
            Integer algorithmCategoryIndex = algorithmNameCategoryMap.get(algorithmNames[i]);
            if (categoryFlagsForProvider[algorithmCategoryIndex + offset]) {
                useKaeProviderFlags[i] = true;
            }
        }

        if (kaeDebug != null) {
            kaeDebug.println("useKaeProviderPropertyNames: ");
            for (int i = 0; i < categoryFlagsForProvider.length; i++) {
                kaeDebug.println(useKaeProviderPropertyNames[i] + "=" + categoryFlagsForProvider[i]);
            }

            kaeDebug.println("useKaeProviderFlags: ");
            for (int i = 0; i < useKaeProviderFlags.length; i++) {
                kaeDebug.println(algorithmNames[i] + "=" + useKaeProviderFlags[i]);
            }
        }
    }

    public static boolean[] getUseKaeProviderFlags() {
        return useKaeProviderFlags;
    }

    private static void initUseKaeEngineFlags() {
        boolean[] categoryFlagsForEngine = new boolean[]{
                true,  // digest
                false, // aes
                true,  // sm4
                false, // hmac
                true,  // rsa
                true,  // dh
                false  // ec
        };
        for (int i = 0; i < useKaeEnginePropertyNames.length; i++) {
            String configValue = privilegedGetOverridable(useKaeEnginePropertyNames[i]);
            if (configValue != null) {
                categoryFlagsForEngine[i] = Boolean.parseBoolean(configValue);
            }
        }

        // EC algorithm currently does not support KAE hardware acceleration, temporarily use openssl soft calculation.
        categoryFlagsForEngine[useKaeEnginePropertyNames.length - 1] = false;

        for (int i = 0; i < useKaeEngineFlags.length; i++) {
            Integer algorithmCategoryIndex = algorithmNameCategoryMap.get(algorithmNames[i]);
            if (categoryFlagsForEngine[algorithmCategoryIndex]) {
                useKaeEngineFlags[i] = true;
            }
        }

        String[] disabledAlgorithms = getDisabledAlgorithms();
        for (String disabledAlgorithm : disabledAlgorithms) {
            Integer algorithmIndex = algorithmNameIndexMap.get(disabledAlgorithm);
            if (algorithmIndex != null) {
                useKaeEngineFlags[algorithmIndex] = false;
            }
        }
        if (kaeDebug != null) {
            kaeDebug.println("useKaeEnginePropertyNames: ");
            for (int i = 0; i < categoryFlagsForEngine.length; i++) {
                kaeDebug.println(useKaeEnginePropertyNames[i] + "=" + categoryFlagsForEngine[i]);
            }

            kaeDebug.println("disabledAlgorithms: ");
            for (int i = 0; i < disabledAlgorithms.length; i++) {
                kaeDebug.println(disabledAlgorithms[i]);
            }

            kaeDebug.println("useKaeEngineFlags: ");
            for (int i = 0; i < useKaeEngineFlags.length; i++) {
                kaeDebug.println(algorithmNames[i] + "=" + useKaeEngineFlags[i]);
            }
        }
    }

    public static boolean[] getUseKaeEngineFlags() {
        return useKaeEngineFlags;
    }

    private static void initAlgorithmNameIndexMap() {
        for (int i = 0; i < algorithmNames.length; i++) {
            algorithmNameIndexMap.put(algorithmNames[i], i);
        }
    }

    /*
     * 0 : digest
     * 1 : aes
     * 2 : sm4
     * 3 : hmac
     * 4 : rsa
     * 5 : dh
     * 6 : ec
     */
    private static void initAlgorithmNameCategoryMap() {
        algorithmNameCategoryMap.put("md5", 0);
        algorithmNameCategoryMap.put("sha256", 0);
        algorithmNameCategoryMap.put("sha384", 0);
        algorithmNameCategoryMap.put("sm3", 0);
        algorithmNameCategoryMap.put("aes-128-ecb", 1);
        algorithmNameCategoryMap.put("aes-128-cbc", 1);
        algorithmNameCategoryMap.put("aes-128-ctr", 1);
        algorithmNameCategoryMap.put("aes-128-gcm", 1);
        algorithmNameCategoryMap.put("aes-192-ecb", 1);
        algorithmNameCategoryMap.put("aes-192-cbc", 1);
        algorithmNameCategoryMap.put("aes-192-ctr", 1);
        algorithmNameCategoryMap.put("aes-192-gcm", 1);
        algorithmNameCategoryMap.put("aes-256-ecb", 1);
        algorithmNameCategoryMap.put("aes-256-cbc", 1);
        algorithmNameCategoryMap.put("aes-256-ctr", 1);
        algorithmNameCategoryMap.put("aes-256-gcm", 1);
        algorithmNameCategoryMap.put("sm4-ecb", 2);
        algorithmNameCategoryMap.put("sm4-cbc", 2);
        algorithmNameCategoryMap.put("sm4-ctr", 2);
        algorithmNameCategoryMap.put("sm4-ofb", 2);
        algorithmNameCategoryMap.put("hmac-md5", 3);
        algorithmNameCategoryMap.put("hmac-sha1", 3);
        algorithmNameCategoryMap.put("hmac-sha224", 3);
        algorithmNameCategoryMap.put("hmac-sha256", 3);
        algorithmNameCategoryMap.put("hmac-sha384", 3);
        algorithmNameCategoryMap.put("hmac-sha512", 3);
        algorithmNameCategoryMap.put("rsa", 4);
        algorithmNameCategoryMap.put("dh", 5);
        algorithmNameCategoryMap.put("ec", 6);
    }

    private static void initAlgorithmNameMap() {
        initAlgorithmNameIndexMap();
        initAlgorithmNameCategoryMap();
    }

    private static String[] getDisabledAlgorithms() {
        String disabledAlgorithms = privilegedGetOverridable("kae.engine.disabledAlgorithms",
                "sha256,sha384");
        return disabledAlgorithms.replaceAll(" ", "").split("\\,");
    }

    public static String privilegedGetProperty(String key) {
        if (System.getSecurityManager() == null) {
            return getProperty(key);
        } else {
            return AccessController.doPrivileged((PrivilegedAction<String>) () -> getOverridableProperty(key));
        }
    }

    public static String privilegedGetOverridable(String key) {
        if (System.getSecurityManager() == null) {
            return getOverridableProperty(key);
        } else {
            return AccessController.doPrivileged((PrivilegedAction<String>) () -> getOverridableProperty(key));
        }
    }

    public static String privilegedGetOverridable(String key, String defaultValue) {
        String val = privilegedGetOverridable(key);
        return (val == null) ? defaultValue : val;
    }

    private static String getProperty(String key) {
        String val = props.getProperty(key);
        if (val != null)
            val = val.trim();
        return val;
    }

    private static String getOverridableProperty(String key) {
        String val = System.getProperty(key);
        if (val == null) {
            return getProperty(key);
        } else {
            return val;
        }
    }

    public static String getAlgorithmName(int index) {
        if (index < 0 || index >= algorithmNames.length) {
            throw new IndexOutOfBoundsException();
        }
        return algorithmNames[index];
    }
}
