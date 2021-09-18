/*
 * Copyright (c) 2009, 2018, Oracle and/or its affiliates. All rights reserved.
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

import sun.security.ec.ECKeyFactory;

import java.math.BigInteger;
import java.security.InvalidAlgorithmParameterException;
import java.security.InvalidParameterException;
import java.security.InvalidKeyException;
import java.security.Key;
import java.security.NoSuchAlgorithmException;
import java.security.PrivateKey;
import java.security.SecureRandom;
import java.security.interfaces.ECPrivateKey;
import java.security.interfaces.ECPublicKey;
import java.security.spec.AlgorithmParameterSpec;
import java.security.spec.ECParameterSpec;
import java.security.spec.ECPoint;
import javax.crypto.KeyAgreementSpi;
import javax.crypto.SecretKey;
import javax.crypto.ShortBufferException;
import javax.crypto.spec.SecretKeySpec;

public class KAEECDHKeyAgreement extends KeyAgreementSpi {
    private ECPrivateKey privateKey;
    private ECPublicKey publicKey;

    // Length of the secret to be derived.
    private int expectedSecretLen;
    private String curveName;

    @Override
    protected void engineInit(Key key, SecureRandom random) throws InvalidKeyException {
        if (!(key instanceof PrivateKey)) {
            throw new InvalidKeyException("Key must be instance of PrivateKey");
        }
        privateKey = (ECPrivateKey) ECKeyFactory.toECKey(key);
        publicKey = null;
    }

    @Override
    protected void engineInit(Key key, AlgorithmParameterSpec params, SecureRandom random)
            throws InvalidKeyException, InvalidAlgorithmParameterException {
        if (params != null) {
            throw new InvalidAlgorithmParameterException("Parameters not supported");
        }
        engineInit(key, random);
    }

    @Override
    protected Key engineDoPhase(Key key, boolean lastPhase) throws InvalidKeyException, IllegalStateException {
        if (privateKey == null) {
            throw new IllegalStateException("Not initialized");
        }
        if (publicKey != null) {
            throw new IllegalStateException("Phase already executed");
        }
        if (!lastPhase) {
            throw new IllegalStateException
                ("Only two party agreement supported, lastPhase must be true");
        }
        if (!(key instanceof ECPublicKey)) {
            throw new InvalidKeyException
                ("Key must be a PublicKey with algorithm EC");
        }

        publicKey = (ECPublicKey) key;
        ECParameterSpec params = publicKey.getParams();
        int keyLenBits = params.getCurve().getField().getFieldSize();
        // Bits to bytes.
        expectedSecretLen = (keyLenBits + 7) >> 3;

        curveName = KAEUtils.getCurveBySize(keyLenBits);
        if (curveName == null) {
            throw new InvalidParameterException("unknown keyLenBits " + keyLenBits);
        }
        if (KAEUtils.getCurveByAlias(curveName) != null) {
            curveName = KAEUtils.getCurveByAlias(curveName);
        }
        return null;
    }

    @Override
    protected byte[] engineGenerateSecret() throws IllegalStateException {
        if ((privateKey == null) || (publicKey == null)) {
            throw new IllegalStateException("Not initialized correctly");
        }
        ECPoint w = publicKey.getW();
        BigInteger wX = w.getAffineX();
        BigInteger wY = w.getAffineY();

        BigInteger s = privateKey.getS();
        byte[] secret = nativeGenerateSecret(curveName, wX.toByteArray(), wY.toByteArray(), s.toByteArray());
        if (secret == null || secret.length != expectedSecretLen) {
            throw new RuntimeException("nativeGenerateSecret error. Expected: " + expectedSecretLen + ", actual: " + (secret == null ? "null" : secret.length));
        }
        return secret;
    }

    @Override
    protected int engineGenerateSecret(byte[] sharedSecret, int offset) throws IllegalStateException, ShortBufferException {
        if (offset + expectedSecretLen > sharedSecret.length) {
            throw new ShortBufferException("Need " + expectedSecretLen +
                    " bytes, only " + (sharedSecret.length - offset) + "available");
        }
        byte[] secret = engineGenerateSecret();
        System.arraycopy(secret, 0, sharedSecret, offset, secret.length);
        return secret.length;
    }

    @Override
    protected SecretKey engineGenerateSecret(String algorithm) throws IllegalStateException,
            NoSuchAlgorithmException, InvalidKeyException {
        if (algorithm == null) {
            throw new NoSuchAlgorithmException("Algorithm must not be null");
        }
        return new SecretKeySpec(engineGenerateSecret(), algorithm);
    }

    protected static native byte[] nativeGenerateSecret(String curveName, byte[] wX, byte[] wY, byte[] s);
}
