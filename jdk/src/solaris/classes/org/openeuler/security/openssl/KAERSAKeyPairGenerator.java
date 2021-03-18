/*
 * Copyright (c) 2003, 2020, Oracle and/or its affiliates. All rights reserved.
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

import sun.security.rsa.*;
import sun.security.rsa.RSAUtil.KeyType;
import sun.security.util.SecurityProviderConstants;

import java.math.BigInteger;
import java.security.*;
import java.security.spec.AlgorithmParameterSpec;
import java.security.spec.RSAKeyGenParameterSpec;

public abstract class KAERSAKeyPairGenerator extends KeyPairGeneratorSpi {
    // public exponent to use
    private BigInteger publicExponent;

    // size of the key to generate, >= KAERSAKeyFactory.MIN_MODLEN
    private int keySize;

    private final KeyType type;

    private AlgorithmParameterSpec keyParams;


    KAERSAKeyPairGenerator(KeyType keyType, int keySize) {
        this.type = keyType;
        initialize(keySize, null);
    }

    // initialize the generator. See JCA doc
    @Override
    public void initialize(int keySize, SecureRandom random) {
        try {
            initialize(new RSAKeyGenParameterSpec(keySize,
                    RSAKeyGenParameterSpec.F4), null);
        } catch (InvalidAlgorithmParameterException iape) {
            throw new InvalidParameterException(iape.getMessage());
        }
    }

    // second initialize method. See JCA doc
    @Override
    public void initialize(AlgorithmParameterSpec params, SecureRandom random)
            throws InvalidAlgorithmParameterException {
        if (!(params instanceof RSAKeyGenParameterSpec)) {
            throw new InvalidAlgorithmParameterException
                    ("Params must be instance of RSAKeyGenParameterSpec");
        }

        RSAKeyGenParameterSpec rsaSpec = (RSAKeyGenParameterSpec) params;
        int tmpKeySize = rsaSpec.getKeysize();
        BigInteger tmpPublicExponent = rsaSpec.getPublicExponent();
        keyParams = rsaSpec.getKeyParams();

        if (tmpPublicExponent == null) {
            tmpPublicExponent = RSAKeyGenParameterSpec.F4;
        } else {
            if (tmpPublicExponent.compareTo(RSAKeyGenParameterSpec.F0) < 0) {
                throw new InvalidAlgorithmParameterException
                        ("Public exponent must be 3 or larger");
            }
            if (tmpPublicExponent.bitLength() > tmpKeySize) {
                throw new InvalidAlgorithmParameterException
                        ("Public exponent must be smaller than key size");
            }
        }

        // do not allow unreasonably large key sizes, probably user error
        try {
            RSAKeyFactory.checkKeyLengths(tmpKeySize, tmpPublicExponent,
                    512, 64 * 1024);
        } catch (InvalidKeyException e) {
            throw new InvalidAlgorithmParameterException(
                    "Invalid key sizes", e);
        }

        this.keySize = tmpKeySize;
        this.publicExponent = tmpPublicExponent;
    }

    // generate the keypair. See JCA doc
    @Override
    public KeyPair generateKeyPair() {
        // get the KAE RSA key Parameters
        byte[][] params = nativeGenerateKeyPair(keySize, publicExponent.toByteArray());

        try {
            // check KAE RSA key Parameters
            checkKAERSAParams(params);

            BigInteger n = new BigInteger(params[0]);
            BigInteger e = new BigInteger(params[1]);
            BigInteger d = new BigInteger(params[2]);
            BigInteger p = new BigInteger(params[3]);
            BigInteger q = new BigInteger(params[4]);
            BigInteger pe = new BigInteger(params[5]);
            BigInteger qe = new BigInteger(params[6]);
            BigInteger coeff = new BigInteger(params[7]);

            // public key
            PublicKey publicKey = RSAPublicKeyImpl.newKey(type, keyParams, n, e);

            // private key
            PrivateKey privateKey = RSAPrivateCrtKeyImpl.newKey(type, keyParams, n, e, d, p, q, pe, qe, coeff);

            return new KeyPair(publicKey, privateKey);
        } catch (InvalidKeyException ex) {
            throw new RuntimeException(ex);
        }
    }

    // check KAE RSA key Parameters
    private void checkKAERSAParams(byte[][] params) throws InvalidKeyException {
        if (params == null || params.length < 8) {
            throw new InvalidKeyException("Invalid KAE RSA key Parameter");
        }

        for (int i = 0; i < params.length; i++) {
            if (params[i] == null) {
                throw new InvalidKeyException("Invalid KAE RSA key Parameter , params[" + i + "] = null");
            }
        }
    }

    public static final class Legacy extends KAERSAKeyPairGenerator {
        public Legacy() {
            super(KeyType.RSA, SecurityProviderConstants.DEF_RSA_KEY_SIZE);
        }
    }

    public static final class PSS extends KAERSAKeyPairGenerator {
        public PSS() {
            super(KeyType.PSS, SecurityProviderConstants.DEF_RSASSA_PSS_KEY_SIZE);
        }
    }

    // generate key pair
    static native byte[][] nativeGenerateKeyPair(int keySize, byte[] publicExponent) throws RuntimeException;
}
