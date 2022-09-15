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

import java.util.HashSet;
import java.util.Set;

/*
 * @test
 * @summary Test property kae.engine.disableAlgorithms
 * @requires os.arch=="aarch64"
 * @run main/othervm -Dkae.engine.disabledAlgorithms=md5 KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.disabledAlgorithms=sha256 KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.disabledAlgorithms=sha384 KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.disabledAlgorithms=sm3 KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.disabledAlgorithms=aes-128-ecb KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.disabledAlgorithms=aes-128-cbc KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.disabledAlgorithms=aes-128-ctr KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.disabledAlgorithms=aes-128-gcm KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.disabledAlgorithms=aes-192-ecb KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.disabledAlgorithms=aes-192-cbc KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.disabledAlgorithms=aes-192-ctr KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.disabledAlgorithms=aes-192-gcm KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.disabledAlgorithms=aes-256-ecb KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.disabledAlgorithms=aes-256-cbc KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.disabledAlgorithms=aes-256-ctr KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.disabledAlgorithms=aes-256-gcm KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.disabledAlgorithms=sm4-ecb KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.disabledAlgorithms=sm4-cbc KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.disabledAlgorithms=sm4-ctr KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.disabledAlgorithms=sm4-ofb KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.disabledAlgorithms=hmac-md5 KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.disabledAlgorithms=hmac-sha1 KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.disabledAlgorithms=hmac-sha224 KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.disabledAlgorithms=hmac-sha256 KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.disabledAlgorithms=hmac-sha384 KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.disabledAlgorithms=hmac-sha512 KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.disabledAlgorithms=rsa KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.disabledAlgorithms=dh KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.disabledAlgorithms=ec KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.disabledAlgorithms=aes-128-gcm,aes-192-gcm,aes-256-gcm KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.disabledAlgorithms=md5,aes-128-ecb,sm4-ecb,hmac-sha1,rsa,dh,ec KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.engine.disabledAlgorithms=md5 KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.engine.disabledAlgorithms=sha256 KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.engine.disabledAlgorithms=sha384 KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.engine.disabledAlgorithms=sm3 KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.engine.disabledAlgorithms=aes-128-ecb KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.engine.disabledAlgorithms=aes-128-cbc KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.engine.disabledAlgorithms=aes-128-ctr KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.engine.disabledAlgorithms=aes-128-gcm KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.engine.disabledAlgorithms=aes-192-ecb KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.engine.disabledAlgorithms=aes-192-cbc KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.engine.disabledAlgorithms=aes-192-ctr KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.engine.disabledAlgorithms=aes-192-gcm KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.engine.disabledAlgorithms=aes-256-ecb KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.engine.disabledAlgorithms=aes-256-cbc KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.engine.disabledAlgorithms=aes-256-ctr KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.engine.disabledAlgorithms=aes-256-gcm KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.engine.disabledAlgorithms=sm4-ecb KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.engine.disabledAlgorithms=sm4-cbc KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.engine.disabledAlgorithms=sm4-ctr KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.engine.disabledAlgorithms=sm4-ofb KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.engine.disabledAlgorithms=hmac-md5 KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.engine.disabledAlgorithms=hmac-sha1 KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.engine.disabledAlgorithms=hmac-sha224 KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.engine.disabledAlgorithms=hmac-sha256 KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.engine.disabledAlgorithms=hmac-sha384 KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.engine.disabledAlgorithms=hmac-sha512 KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.engine.disabledAlgorithms=rsa KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.engine.disabledAlgorithms=dh KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.engine.disabledAlgorithms=ec KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.engine.disabledAlgorithms=aes-128-gcm,aes-192-gcm,aes-256-gcm KAEDisabledAlgorithmsTest
 * @run main/othervm -Dkae.engine.id=uadk_engine -Dkae.libcrypto.useGlobalMode=true -Dkae.engine.disabledAlgorithms=md5,aes-128-ecb,sm4-ecb,hmac-sha1,rsa,dh,ec KAEDisabledAlgorithmsTest
 */
public class KAEDisabledAlgorithmsTest {

    public static void main(String[] args) {
        KAETestHelper.Engine engine = KAETestHelper.getEngine();
        if (!engine.isValid()) {
            System.out.println("Skip test, engine " + engine.getEngineId() + " does not exist.");
            return;
        }
        String[] disabledAlgorithms = getDisabledAlgorithms();
        init();
        new KAEProvider();
        test(disabledAlgorithms);
    }

    private static final String[] PROPERTY_NAMES = new String[]{
            "kae.digest.useKaeEngine",
            "kae.aes.useKaeEngine",
            "kae.sm4.useKaeEngine",
            "kae.hmac.useKaeEngine",
            "kae.rsa.useKaeEngine",
            "kae.dh.useKaeEngine",
            "kae.ec.useKaeEngine"
    };

    private static String[] getDisabledAlgorithms() {
        String value = System.getProperty("kae.engine.disabledAlgorithms");
        if (value == null) {
            return new String[0];
        }
        return value.split(",");
    }

    private static void init() {
        for (String propertyName : PROPERTY_NAMES) {
            System.setProperty(propertyName, "true");
        }
    }

    private static void test(String[] disabledAlgorithms) {
        boolean[] useKaeEngineFlags = KAEConfig.getUseKaeEngineFlags();
        Set<Integer> disabledAlgorithmIndexSet = new HashSet<>();

        // test disabled algorithms
        for (String disabledAlgorithm : disabledAlgorithms) {
            Integer index = KAETestHelper.getAlgorithmIndex(disabledAlgorithm);
            if (index == null || index < 0 || index >= useKaeEngineFlags.length) {
                continue;
            }
            if (useKaeEngineFlags[index]) {
                throw new RuntimeException("test failed");
            }
            disabledAlgorithmIndexSet.add(index);
        }

        // test other algorithms that are not disabled (except ec)
        for (int i = 0; i < useKaeEngineFlags.length - 1; i++) {
            if (!disabledAlgorithmIndexSet.contains(i) && !useKaeEngineFlags[i]) {
                throw new RuntimeException(KAETestHelper.getAlgorithmName(i) + " algorithm is not disabled");
            }
        }

        // test whether the ec algorithm is disabled by default
        if (useKaeEngineFlags[useKaeEngineFlags.length - 1]) {
            throw new RuntimeException(KAETestHelper.getAlgorithmName(useKaeEngineFlags.length - 1)
                    + " algorithm is disabled by default");
        }
    }
}
