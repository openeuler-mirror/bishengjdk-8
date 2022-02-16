/*
 * Copyright (c) 2003, 2014, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2021, Huawei Technologies Co., Ltd. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
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

import sun.security.ec.ECPrivateKeyImpl;
import sun.security.ec.ECPublicKeyImpl;

import java.math.BigInteger;
import java.security.InvalidAlgorithmParameterException;
import java.security.InvalidParameterException;
import java.security.InvalidKeyException;
import java.security.KeyPair;
import java.security.KeyPairGeneratorSpi;
import java.security.ProviderException;
import java.security.SecureRandom;
import java.security.spec.AlgorithmParameterSpec;
import java.security.spec.ECFieldFp;
import java.security.spec.ECField;
import java.security.spec.ECGenParameterSpec;
import java.security.spec.ECParameterSpec;
import java.security.spec.ECPoint;
import java.security.spec.EllipticCurve;
import java.util.HashMap;
import java.util.Map;

public class KAEECKeyPairGenerator extends KeyPairGeneratorSpi {
    private ECParameterSpec param = null;
    private final int defaultKeySize = 256;

    @Override
    public void initialize(int keysize, SecureRandom random) {
        String curveName = KAEUtils.getCurveBySize(keysize);
        if (curveName == null) {
            throw new InvalidParameterException("unknown key size " + keysize);
        }
        if (KAEUtils.getCurveByAlias(curveName) != null) {
            curveName = KAEUtils.getCurveByAlias(curveName);
        }
        this.param = getParamsByCurve(curveName);
    }

    private ECParameterSpec getParamsByCurve(String curveName) {
        byte[][] params = nativeGenerateParam(curveName);
        // check params
        checkParams(params, curveName);
        BigInteger p = new BigInteger(params[0]);
        BigInteger a = new BigInteger(params[1]);
        BigInteger b = new BigInteger(params[2]);
        BigInteger x = new BigInteger(params[3]);
        BigInteger y = new BigInteger(params[4]);
        BigInteger order = new BigInteger(params[5]);
        BigInteger cofactor = new BigInteger(params[6]);
        ECField field = new ECFieldFp(p);
        EllipticCurve curve = new EllipticCurve(field, a, b);
        ECPoint g = new ECPoint(x, y);
        ECParameterSpec spec = new ECParameterSpec(curve, g, order, cofactor.intValue());
        return spec;
    }

    private void checkParams(byte[][] params, String curveName) {
        if (params == null) {
            throw new InvalidParameterException("Unknown curve " + curveName);
        }
        // The params needs to contain at least 7 byte arrays, which are p,a,b,x,y,order and cofactor.
        if (params.length < 7) {
            throw new InvalidParameterException("The params length is less than 7.");
        }
        for (int i = 0; i < params.length; i++) {
            if (params[i] == null) {
                throw new InvalidParameterException("The params[" + i + "]" + "is null.");
            }
        }
    }

    @Override
    public void initialize(AlgorithmParameterSpec param, SecureRandom random) throws InvalidAlgorithmParameterException {
        if (param instanceof ECParameterSpec) {
            this.param = (ECParameterSpec) param;
        } else if (param instanceof ECGenParameterSpec) {
            ECGenParameterSpec ecParam = (ECGenParameterSpec)param;
            String curveName = ecParam.getName();
            if (KAEUtils.getCurveByAlias(curveName) != null) {
                curveName = KAEUtils.getCurveByAlias(curveName);
            }
            this.param = getParamsByCurve(curveName);
        } else {
            throw new InvalidAlgorithmParameterException("ECParameterSpec or ECGenParameterSpec for EC");
        }
    }

    @Override
    public KeyPair generateKeyPair() {
        if (param == null) {
            String curveName = KAEUtils.getCurveBySize(defaultKeySize);
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

        ECPrivateKeyImpl privateKey = null;
        ECPublicKeyImpl publicKey = null;
        try {
            publicKey = new ECPublicKeyImpl(w, param);
            privateKey = new ECPrivateKeyImpl(s, param);
        } catch (InvalidKeyException e) {
            throw new ProviderException(e);
        }
        return new KeyPair(publicKey, privateKey);
    }

    protected static native byte[][] nativeGenerateParam(String curveName);

    protected static native byte[][] nativeGenerateKeyPair(byte[] p, byte[] a, byte[] b, byte[] x, byte[] y,
            byte[] order, int cofactor);
}
