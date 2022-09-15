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
import sun.security.ec.ECPrivateKeyImpl;
import sun.security.ec.ECPublicKeyImpl;

import javax.crypto.KeyAgreement;
import java.math.BigInteger;
import java.security.KeyPair;
import java.security.KeyPairGenerator;
import java.security.Provider;
import java.security.Security;
import java.security.spec.ECFieldFp;
import java.security.spec.ECParameterSpec;
import java.security.spec.ECPoint;
import java.security.spec.EllipticCurve;
import java.util.Arrays;
import java.nio.charset.StandardCharsets;

/**
 * @test
 * @summary Basic test for ECDH
 * @requires os.arch=="aarch64"
 * @run main ECDHTest
 */

public class ECDHTest {

    private static KeyPairGenerator keyPairGenerator;
    private static String algorithm = "EC";
    private static int[] keyArr = {224, 256, 384, 521};

    public static void main(String[] args) throws Exception {
        Security.insertProviderAt(new KAEProvider(), 1);

        BigInteger a = new BigInteger("26959946667150639794667015087019630673557916260026308143510066298878");
        BigInteger b = new BigInteger("18958286285566608000408668544493926415504680968679321075787234672564");
        BigInteger p = new BigInteger("26959946667150639794667015087019630673557916260026308143510066298881");
        BigInteger x = new BigInteger("19277929113566293071110308034699488026831934219452440156649784352033");
        BigInteger y = new BigInteger("19926808758034470970197974370888749184205991990603949537637343198772");
        EllipticCurve CURVE = new EllipticCurve(new ECFieldFp(p), a, b);
        ECPoint POINT = new ECPoint(x, y);
        BigInteger ORDER = new BigInteger("26959946667150639794667015087019625940457807714424391721682722368061");
        int COFACTOR = 1;
        ECParameterSpec PARAMS = new ECParameterSpec(CURVE, POINT, ORDER, COFACTOR);

        testKeyPairByParam(PARAMS);
        for (int keySize : keyArr) {
            testKeyPairByKeySize(keySize);
        }

        ECPrivateKeyImpl ecPrivKey = new ECPrivateKeyImpl(new BigInteger("20135071615800221517902437867016717688420688735490569283842831828983"), PARAMS);
        ECPoint ecPoint = new ECPoint(new BigInteger("9490267631555585552004372465967099662885480699902812460349461311384"), new BigInteger("1974573604976093871117393045089050409882519645527397292712281520811"));
        ECPublicKeyImpl ecPublicKey = new ECPublicKeyImpl(ecPoint, PARAMS);
        testKeyAgreement(ecPrivKey, ecPublicKey, new byte[]{-88, -65, 43, -84, 26, 43, 46, 106, 20, 39, -76, 30, -71, 72, -102, 120, 108, -92, -86, -14, -96, -42, 93, -40, -43, -25, 15, -62});

    }

    public static void testKeyPairByParam(ECParameterSpec PARAMS) throws Exception {
        keyPairGenerator = KeyPairGenerator.getInstance(algorithm);
        keyPairGenerator.initialize(PARAMS);
        KeyPair keyPair = keyPairGenerator.generateKeyPair();
        ECPrivateKeyImpl ecPrivKey = (ECPrivateKeyImpl) keyPair.getPrivate();
        ECPublicKeyImpl ecPublicKey = (ECPublicKeyImpl) keyPair.getPublic();
    }

    public static void testKeyPairByKeySize(int keySize) throws Exception {
        keyPairGenerator = KeyPairGenerator.getInstance(algorithm);
        keyPairGenerator.initialize(keySize);
        KeyPair keyPair = keyPairGenerator.generateKeyPair();
        ECPrivateKeyImpl ecPrivKey = (ECPrivateKeyImpl) keyPair.getPrivate();
        ECPublicKeyImpl ecPublicKey = (ECPublicKeyImpl) keyPair.getPublic();
    }

    public static void testKeyAgreement(ECPrivateKeyImpl ecPrivKey, ECPublicKeyImpl ecPublicKey, byte[] expectRes) throws Exception {
        KeyAgreement keyAgreement = KeyAgreement.getInstance("ECDH");
        keyAgreement.init(ecPrivKey);
        keyAgreement.doPhase(ecPublicKey, true);
        byte[] res = keyAgreement.generateSecret();
        if (!Arrays.equals(res, expectRes)) {
            throw new RuntimeException("keyagreement failed");
        }
    }
}
