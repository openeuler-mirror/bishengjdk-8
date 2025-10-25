/*
 * Copyright (c) 2024, Huawei Technologies Co., Ltd. All rights reserved.
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

import java.math.BigInteger;
import java.security.InvalidAlgorithmParameterException;
import java.security.InvalidKeyException;
import java.security.InvalidParameterException;
import java.security.KeyPair;
import java.security.ProviderException;
import java.security.SecureRandom;
import java.security.spec.AlgorithmParameterSpec;
import java.security.spec.ECFieldFp;
import java.security.spec.ECGenParameterSpec;
import java.security.spec.ECParameterSpec;
import java.security.spec.ECPoint;
import java.security.spec.EllipticCurve;

public class KAESM2KeyPairGenerator extends KAEECKeyPairGenerator {
    private static final String SUPPORTED_CURVE_NAME = "sm2p256v1";
    private static final int SUPPORTED_KEY_SIZE = 256;
    private ECParameterSpec param = null;

    @Override
    public void initialize(int keysize, SecureRandom random) {
        if (keysize != SUPPORTED_KEY_SIZE) {
            throw new InvalidParameterException("unknown key size " + keysize);
        }
        String curveName = KAEUtils.getCurveByAlias(SUPPORTED_CURVE_NAME);
        param = getParamsByCurve(curveName);
    }

    @Override
    public void initialize(AlgorithmParameterSpec param, SecureRandom random)
            throws InvalidAlgorithmParameterException {
        if (param instanceof ECParameterSpec) {
            this.param = (ECParameterSpec) param;
        } else if (param instanceof ECGenParameterSpec) {
            ECGenParameterSpec ecParam = (ECGenParameterSpec)param;
            if (!SUPPORTED_CURVE_NAME.equals(ecParam.getName())) {
                throw new InvalidAlgorithmParameterException("Only support sm2p256v1");
            }
            String curveName = KAEUtils.getCurveByAlias(SUPPORTED_CURVE_NAME);
            this.param = getParamsByCurve(curveName);
        } else {
            throw new InvalidAlgorithmParameterException("ECParameterSpec or ECGenParameterSpec for EC");
        }
    }

    @Override
    public KeyPair generateKeyPair() {
        if (param == null) {
            String curveName = KAEUtils.getCurveByAlias(SUPPORTED_CURVE_NAME);
            param = getParamsByCurve(curveName);
        }
        EllipticCurve curve = param.getCurve();
        ECFieldFp field = (ECFieldFp) curve.getField();
        BigInteger p = field.getP();
        BigInteger a = curve.getA();
        BigInteger b = curve.getB();
        ECPoint generator = param.getGenerator();
        BigInteger x = generator.getAffineX();
        BigInteger y = generator.getAffineY();
        BigInteger order = param.getOrder();
        int cofactor = param.getCofactor();

        byte[][] keys = nativeGenerateKeyPair(p.toByteArray(), a.toByteArray(),
                b.toByteArray(), x.toByteArray(), y.toByteArray(), order.toByteArray(), cofactor);
        if (keys == null) {
            throw new RuntimeException("nativeGenerateKeyPair failed");
        }
        BigInteger wX = new BigInteger(keys[0]);
        BigInteger wY = new BigInteger(keys[1]);
        BigInteger s = new BigInteger(keys[2]);
        ECPoint w = new ECPoint(wX, wY);

        KAEECPrivateKeyImpl privateKey;
        KAEECPublicKeyImpl publicKey;
        try {
            publicKey = new KAEECPublicKeyImpl(w, param);
            privateKey = new KAEECPrivateKeyImpl(s, param);
        } catch (InvalidKeyException e) {
            throw new ProviderException(e);
        }
        return new KeyPair(publicKey, privateKey);
    }
}
