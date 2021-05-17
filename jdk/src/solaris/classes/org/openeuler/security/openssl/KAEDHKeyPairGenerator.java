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

import java.math.BigInteger;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.security.spec.AlgorithmParameterSpec;
import java.security.KeyPairGeneratorSpi;
import java.security.SecureRandom;
import java.security.InvalidAlgorithmParameterException;
import java.security.KeyPair;
import java.security.Key;
import java.security.NoSuchAlgorithmException;
import java.security.InvalidParameterException;
import java.security.GeneralSecurityException;
import java.security.ProviderException;
import java.security.KeyFactory;
import java.security.spec.InvalidKeySpecException;
import sun.security.jca.JCAUtil;
import sun.security.provider.ParameterCache;
import javax.crypto.spec.DHPublicKeySpec;
import javax.crypto.spec.DHPrivateKeySpec;
import javax.crypto.spec.DHParameterSpec;
import javax.crypto.interfaces.DHPublicKey;
import javax.crypto.interfaces.DHPrivateKey;

import static sun.security.util.SecurityProviderConstants.DEF_DH_KEY_SIZE;

public class KAEDHKeyPairGenerator
        extends KeyPairGeneratorSpi {
    private DHParameterSpec parameterSpec;
    // The size in bits of the random exponent (private value)
    private int pSize;
    // The size in bits of the random exponent (private value)
    private int lSize;
    private SecureRandom random;

    public KAEDHKeyPairGenerator() {
        super();
        initialize(DEF_DH_KEY_SIZE, null);
    }

    private static void checkKeySize(int keySize) {
        if ((keySize < 512) || (keySize > 8192) || ((keySize & 0x3F) != 0)) {
            throw new InvalidParameterException(
                    "DH key size must be multiple of 64, and can only range " +
                            "from 512 to 8192(inclusize).  " +
                            "The specific key size " + keySize + " is not supported");
        }
    }
    @Override
    public void initialize(AlgorithmParameterSpec params, SecureRandom random)
            throws InvalidAlgorithmParameterException {

        if (!(params instanceof DHParameterSpec)){
            throw new InvalidAlgorithmParameterException
                    ("Inappropriate parameter type");
        }

        parameterSpec = (DHParameterSpec) params;
        pSize = parameterSpec.getP().bitLength();

        try {
            checkKeySize(pSize);
        } catch (InvalidParameterException e) {
            throw new InvalidAlgorithmParameterException(e.getMessage());
        }

        // exponent size is optional, could be 0
        lSize = parameterSpec.getL();

        // Require exponentSize < primeSize
        if ((lSize != 0) && (lSize > pSize)) {
            throw new InvalidAlgorithmParameterException
                    ("Exponent size must not be larger than modulus size");
        }

        this.random = random;
    }

    @Override
    public void initialize(int keysize, SecureRandom random) {
        checkKeySize(keysize);
        this.parameterSpec = ParameterCache.getCachedDHParameterSpec(keysize);
        if ((this.parameterSpec == null) && (keysize > 1024)) {
            throw new InvalidParameterException("Unsupported " + keysize + "-bit DH parameter generation.");
        }
        this.pSize = keysize;
        this.lSize = 0;
        this.random = random;
    }

    @Override
    public KeyPair generateKeyPair() {

        if (random == null) {
            random = JCAUtil.getSecureRandom();
        }

        if (parameterSpec == null) {
            try {
                parameterSpec = ParameterCache.getDHParameterSpec(pSize, random);
            } catch (GeneralSecurityException e) {
                // should never happen
                throw new ProviderException(e);
            }
        }

        BigInteger p = parameterSpec.getP();
        BigInteger g = parameterSpec.getG();

        if (lSize <= 0) {
            lSize = pSize >> 1;
            // use an exponent size of (pSize / 2) but at least 384 bits
            if (lSize < 384) {
                lSize = 384;
            }
        }
        byte[][] keys;
        try {
            keys = nativeGenerateKeyPair(p.toByteArray(), g.toByteArray(), lSize);
        } catch (Exception e){
            throw new ProviderException("Invoke nativeGenerateKeyPair failed.", e);
        }

        BigInteger pubKey = new BigInteger(keys[0]);
        BigInteger priKey = new BigInteger(keys[1]);

        try{
            KeyFactory fk = KeyFactory.getInstance("DH");
            DHPublicKey publicKey = (DHPublicKey)fk.generatePublic(new DHPublicKeySpec(pubKey, p , g));
            DHPrivateKey privateKey = (DHPrivateKey)fk.generatePrivate(new DHPrivateKeySpec(priKey, p, g));
            return new KeyPair(publicKey, privateKey);
        } catch (NoSuchAlgorithmException noalg) {
            throw new ProviderException(noalg);
        } catch (InvalidKeySpecException ikse) {
            throw new ProviderException(ikse);
        }
    }
    protected native static byte[][] nativeGenerateKeyPair(byte[] p, byte[] g, int lSize);
}
