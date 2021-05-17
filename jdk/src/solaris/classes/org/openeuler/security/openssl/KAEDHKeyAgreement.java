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

import sun.security.util.KeyUtil;
import java.math.BigInteger;
import java.security.KeyFactory;
import java.security.InvalidKeyException;
import java.security.SecureRandom;
import java.security.Key;
import java.security.InvalidAlgorithmParameterException;
import java.security.NoSuchAlgorithmException;
import java.security.AlgorithmParameterGeneratorSpi;
import java.security.AccessController;
import java.security.ProviderException;
import java.security.PrivilegedAction;
import java.security.spec.InvalidKeySpecException;
import java.security.spec.AlgorithmParameterSpec;
import javax.crypto.KeyAgreementSpi;
import javax.crypto.SecretKey;
import javax.crypto.ShortBufferException;
import javax.crypto.interfaces.DHPrivateKey;
import javax.crypto.interfaces.DHPublicKey;
import javax.crypto.spec.DHParameterSpec;
import javax.crypto.spec.SecretKeySpec;
import javax.crypto.spec.DHPublicKeySpec;
import javax.crypto.SecretKeyFactory;
import javax.crypto.spec.DESedeKeySpec;
import javax.crypto.spec.DESKeySpec;

public class KAEDHKeyAgreement extends KeyAgreementSpi {
    private boolean generateSecret = false;
    private BigInteger p;
    private BigInteger g;
    private BigInteger x;
    private BigInteger y;
    static final int[] AES_KEYSIZES = {16, 24, 32};
    static final int BLOWFISH_MAX_KEYSIZE = 56;

    private static class AllowKDF {
        private static final boolean VALUE = getValue();

        private static boolean getValue() {
            return AccessController.doPrivileged(
                    (PrivilegedAction<Boolean>)
                            () -> Boolean.getBoolean("jdk.crypto.KeyAgreement.legacyKDF"));
        }
    }

    public KAEDHKeyAgreement() {
    }

    @Override
    protected void engineInit(Key key, SecureRandom random) throws InvalidKeyException {
        try {
            engineInit(key, null, random);
        } catch (InvalidAlgorithmParameterException e) {
            // never happens, because we did not pass any parameters
        }
    }

    @Override
    protected void engineInit(Key key, AlgorithmParameterSpec params,
                              SecureRandom random)
            throws InvalidKeyException, InvalidAlgorithmParameterException {

        // ignore "random" parameter, because our implementation does not
        // require any source of randomness
        generateSecret = false;
        p = null;
        g = null;

        if ((params != null) && !(params instanceof DHParameterSpec)) {
            throw new InvalidAlgorithmParameterException("Diffie-Hellman parameters expected");
        }

        if (!(key instanceof DHPrivateKey)) {
            throw new InvalidKeyException("Diffie-Hellman private key expected");
        }

        DHPrivateKey privateKey = (DHPrivateKey) key;

        // check if private key parameters are compatible with
        // initialized ones
        if (params != null) {
            p = ((DHParameterSpec) params).getP();
            g = ((DHParameterSpec) params).getG();
        }

        BigInteger priv_p = privateKey.getParams().getP();
        BigInteger priv_g = privateKey.getParams().getG();
        if (p != null && priv_p != null && !(p.equals(priv_p))) {
            throw new InvalidKeyException("Incompatible parameters");
        }
        if (g != null && priv_g != null && !(g.equals(priv_g))) {
            throw new InvalidKeyException("Incompatible parameters");
        }
        if ((p == null && priv_p == null) ||
                (g == null) && priv_g == null) {
            throw new InvalidKeyException("Missing parameters");
        }
        p = priv_p;
        g = priv_g;

        // store the x value
        x = privateKey.getX();
    }

    @Override
    protected Key engineDoPhase(Key key, boolean lastPhase)
            throws InvalidKeyException, IllegalStateException {
        if (!(key instanceof DHPublicKey)) {
            throw new InvalidKeyException("Diffie-Hellman public "
                    + "expected");
        }
        DHPublicKey publicKey = (DHPublicKey) key;
        if (p == null || g == null) {
            throw new IllegalStateException("Not initialized");
        }
        BigInteger pub_p = publicKey.getParams().getP();
        BigInteger pub_g = publicKey.getParams().getG();
        if (pub_p != null && !(p.equals(pub_p))) {
            throw new InvalidKeyException("Incompatible parameters");
        }
        if (pub_g != null && !(g.equals(pub_g))) {
            throw new InvalidKeyException("Incompatible parameters");
        }
        KeyUtil.validate(publicKey);
        y = publicKey.getY();
        generateSecret = true;
        if (lastPhase == false) {
            byte[] intermediate = engineGenerateSecret();
            try {
                KeyFactory fk = KeyFactory.getInstance("DH");
                DHPublicKey newPublicKey = (DHPublicKey) fk.generatePublic(
                        new DHPublicKeySpec(new BigInteger(1, intermediate), p, g));
                return newPublicKey;
            } catch (NoSuchAlgorithmException noalg) {
                throw new ProviderException(noalg);
            } catch (InvalidKeySpecException ikse) {
                throw new ProviderException(ikse);
            }
        } else {
            return null;
        }
    }

    @Override
    protected byte[] engineGenerateSecret()
            throws IllegalStateException {
        int expectedLen = (p.bitLength() + 7) >>> 3;
        byte[] result = new byte[expectedLen];
        try {
            engineGenerateSecret(result, 0);
        } catch (ShortBufferException shortBufferException) {
            // should never happen since length are identical
        }
        return result;
    }

    @Override
    protected int engineGenerateSecret(byte[] sharedSecret, int offset)
            throws IllegalStateException, ShortBufferException {
        if (!generateSecret) {
            throw new IllegalStateException("Key agreement has not bee complated yet");
        }
        if (sharedSecret == null) {
            throw new ShortBufferException("No buffer provided for shared secret");
        }
        BigInteger modulus = p;
        int expectedLen = (modulus.bitLength() + 7) >>> 3;
        if ((sharedSecret.length - offset) < expectedLen) {
            throw new ShortBufferException("Buffer too short for shared secret");
        }
        generateSecret = false;
        byte[] secret = nativeComputeKey(y.toByteArray(), x.toByteArray(),
                p.toByteArray(), g.toByteArray(), modulus.bitLength());

        if (secret.length == expectedLen) {
            System.arraycopy(secret, 0, sharedSecret, offset,
                    secret.length);
        } else {
            // Array too short, pad it w/ leading 0s
            if (secret.length < expectedLen) {
                System.arraycopy(secret, 0, sharedSecret,
                        offset + (expectedLen - secret.length),
                        secret.length);
            } else {
                // Array too long, check and trim off the excess
                if ((secret.length == (expectedLen + 1)) && secret[0] == 0) {
                    // ignore the leading sign byte
                    System.arraycopy(secret, 1, sharedSecret, offset, expectedLen);
                } else {
                    throw new ProviderException("Generated secret is out-of-range");
                }
            }
        }

        return expectedLen;
    }

    @Override
    protected SecretKey engineGenerateSecret(String algorithm)
            throws IllegalStateException, NoSuchAlgorithmException, InvalidKeyException {
        if (algorithm == null) {
            throw new NoSuchAlgorithmException("null algorithm");
        }

        if (!algorithm.equalsIgnoreCase("TlsPremasterSecret") &&
                !AllowKDF.VALUE) {

            throw new NoSuchAlgorithmException("Unsupported secret key "
                    + "algorithm: " + algorithm);
        }

        byte[] secret = engineGenerateSecret();
        if (algorithm.equalsIgnoreCase("DES")) {
            // DES
            try {
                SecretKeyFactory factory = SecretKeyFactory.getInstance("DES");
                return factory.generateSecret(new DESKeySpec(secret));
            } catch (InvalidKeySpecException e) {
                throw new ProviderException("Generate DES Secret failed.", e);
            }
        } else if (algorithm.equalsIgnoreCase("DESede")
                || algorithm.equalsIgnoreCase("TripleDES")) {
            // Triple DES
            try {
                SecretKeyFactory factory = SecretKeyFactory.getInstance("DESede");
                return factory.generateSecret(new DESedeKeySpec(secret));
            } catch (InvalidKeySpecException e) {
                throw new ProviderException("Generate DESede Secret failed.", e);
            }
        } else if (algorithm.equalsIgnoreCase("Blowfish")) {
            // Blowfish
            int keysize = secret.length;
            if (keysize >= BLOWFISH_MAX_KEYSIZE)
                keysize = BLOWFISH_MAX_KEYSIZE;
            return new SecretKeySpec(secret, 0, keysize, "Blowfish");
        } else if (algorithm.equalsIgnoreCase("AES")) {
            int idx = AES_KEYSIZES.length - 1;
            int keysize = secret.length;
            SecretKeySpec secretKey = null;
            while (secretKey == null && idx >= 0) {
                if (keysize >= AES_KEYSIZES[idx]) {
                    keysize = AES_KEYSIZES[idx];
                    secretKey = new SecretKeySpec(secret, 0, keysize, "AES");
                }
                idx--;
            }
            if (secretKey == null) {
                throw new InvalidKeyException("Key material is too short");
            }
            return secretKey;
        } else if (algorithm.equals("TlsPremasterSecret")) {
            // remove leading zero bytes per RFC 5246 Section 8.1.2
            return new SecretKeySpec(
                    KeyUtil.trimZeroes(secret), "TlsPremasterSecret");
        } else {
            throw new NoSuchAlgorithmException("Unsupported secret key "
                    + "algorithm: " + algorithm);
        }
    }
    protected native byte[] nativeComputeKey(byte[] y, byte[] x, byte[] p, byte[] g, int pSize);
}
