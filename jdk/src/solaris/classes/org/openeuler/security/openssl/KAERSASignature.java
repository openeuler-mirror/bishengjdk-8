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

import java.io.IOException;
import java.nio.ByteBuffer;

import java.security.*;
import java.security.interfaces.*;
import java.security.spec.AlgorithmParameterSpec;

import sun.security.rsa.RSACore;
import sun.security.rsa.RSAKeyFactory;
import sun.security.rsa.RSAPadding;
import sun.security.rsa.RSAUtil;
import sun.security.rsa.RSAUtil.KeyType;
import sun.security.util.*;
import sun.security.x509.AlgorithmId;

import javax.crypto.BadPaddingException;

/**
 * We support support rsa signatures with MD2, MD5, SHA-1, SHA-224, SHA-256, SHA-384, SHA-512 as the digest algorithm.
 * The Openssl does not support rsa signatures with SHA-512/224 and SHA-512/256 as the digest algorithm,
 * so we have not implemented them.
 * The Openssl does not support non-CRT private key , when signing with a non-CRT private key, we use the sun sign.
 */
public abstract class KAERSASignature extends SignatureSpi {
    // we sign an ASN.1 SEQUENCE of AlgorithmId and digest
    // it has the form 30:xx:30:xx:[digestOID]:05:00:04:xx:[digest]
    // this means the encoded length is (8 + digestOID.length + digest.length)
    private static final int BASE_LENGTH = 8;

    private String digestAlgorithm;

    // object identifier for the message digest algorithm used
    private final ObjectIdentifier digestOID;

    // length of the encoded signature blob
    private final int encodedLength;

    // message digest implementation we use
    private final MessageDigest md;

    // flag indicating whether the digest is reset
    private boolean digestReset;

    // private key, if initialized for signing
    private RSAPrivateKey privateKey;

    // public key, if initialized for verifying
    private RSAPublicKey publicKey;

    // padding to use, set when the initSign/initVerify is called
    private RSAPadding padding;

    /**
     * Construct a new RSASignature. Used by subclasses.
     */
    KAERSASignature(String algorithm, ObjectIdentifier digestOID, int oidLength) {
        this.digestAlgorithm = algorithm;
        this.digestOID = digestOID;
        try {
            md = MessageDigest.getInstance(algorithm);
        } catch (NoSuchAlgorithmException e) {
            throw new ProviderException(e);
        }
        digestReset = true;
        encodedLength = BASE_LENGTH + oidLength + md.getDigestLength();
    }

    // initialize for verification. See JCA doc
    @Override
    protected void engineInitVerify(PublicKey publicKey) throws InvalidKeyException {
        RSAPublicKey rsaKey = (RSAPublicKey) RSAKeyFactory.toRSAKey(publicKey);
        this.privateKey = null;
        this.publicKey = rsaKey;
        initCommon(rsaKey, null);
    }

    // initialize for signing. See JCA doc
    @Override
    protected void engineInitSign(PrivateKey privateKey) throws InvalidKeyException {
        engineInitSign(privateKey, null);
    }

    // initialize for signing. See JCA doc
    @Override
    protected void engineInitSign(PrivateKey privateKey, SecureRandom random) throws InvalidKeyException {
        RSAPrivateKey rsaKey =
                (RSAPrivateKey) RSAKeyFactory.toRSAKey(privateKey);
        this.privateKey = rsaKey;
        this.publicKey = null;
        initCommon(rsaKey, random);
    }

    /**
     * Init code common to sign and verify.
     */
    private void initCommon(RSAKey rsaKey, SecureRandom random) throws InvalidKeyException {
        try {
            RSAUtil.checkParamsAgainstType(KeyType.RSA, rsaKey.getParams());
        } catch (ProviderException e) {
            throw new InvalidKeyException("Invalid key for RSA signatures", e);
        }
        resetDigest();
        int keySize = RSACore.getByteLength(rsaKey);
        try {
            padding = RSAPadding.getInstance
                    (RSAPadding.PAD_BLOCKTYPE_1, keySize, random);
        } catch (InvalidAlgorithmParameterException iape) {
            throw new InvalidKeyException(iape.getMessage());
        }
        int maxDataSize = padding.getMaxDataSize();
        if (encodedLength > maxDataSize) {
            throw new InvalidKeyException
                    ("Key is too short for this signature algorithm");
        }
    }

    /**
     * Reset the message digest if it is not already reset.
     */
    private void resetDigest() {
        if (digestReset == false) {
            md.reset();
            digestReset = true;
        }
    }

    /**
     * Return the message digest value.
     */
    private byte[] getDigestValue() {
        digestReset = true;
        return md.digest();
    }

    // update the signature with the plaintext data. See JCA doc
    @Override
    protected void engineUpdate(byte b) throws SignatureException {
        md.update(b);
        digestReset = false;
    }

    // update the signature with the plaintext data. See JCA doc
    @Override
    protected void engineUpdate(byte[] b, int off, int len) throws SignatureException {
        md.update(b, off, len);
        digestReset = false;
    }

    // update the signature with the plaintext data. See JCA doc
    @Override
    protected void engineUpdate(ByteBuffer b) {
        md.update(b);
        digestReset = false;
    }

    // sign the data and return the signature. See JCA doc
    @Override
    protected byte[] engineSign() throws SignatureException {
        if (privateKey == null) {
            throw new SignatureException("Missing private key");
        }

        byte[] digest = getDigestValue();
        if (useKaeSign()) {
            return kaeSign(digest);
        }
        return sunSign(digest);
    }

    // determine if use kae sign , openssl do not support non-CRT private key
    private boolean useKaeSign() {
        return privateKey instanceof RSAPrivateCrtKey;
    }

    // sun sign
    private byte[] sunSign(byte[] digest) throws SignatureException {
        try {
            byte[] encoded = encodeSignature(digestOID, digest);
            byte[] padded = padding.pad(encoded);
            return RSACore.rsa(padded, privateKey, true);
        } catch (GeneralSecurityException e) {
            throw new SignatureException("Could not sign data", e);
        } catch (IOException e) {
            throw new SignatureException("Could not encode data", e);
        }
    }

    // kae sign
    private byte[] kaeSign(byte[] digest) throws SignatureException {
        String kaeDigestName = KAEUtils.getKAEDigestName(this.digestAlgorithm);
        RSAPrivateCrtKey privateCrtKey = (RSAPrivateCrtKey) privateKey;
        long keyAddress = KAERSACipher.nativeCreateRSAPrivateCrtKey(
                privateCrtKey.getModulus().toByteArray(),
                privateCrtKey.getPublicExponent().toByteArray(),
                privateCrtKey.getPrivateExponent().toByteArray(),
                privateCrtKey.getPrimeP().toByteArray(),
                privateCrtKey.getPrimeQ().toByteArray(),
                privateCrtKey.getPrimeExponentP().toByteArray(),
                privateCrtKey.getPrimeExponentQ().toByteArray(),
                privateCrtKey.getCrtCoefficient().toByteArray());
        byte[] sigBytes;
        try {
            sigBytes = KAERSASignatureNative.rsaSign(keyAddress,
                    kaeDigestName, digest, KAERSAPaddingType.PKCS1Padding.getId());
        } catch (SignatureException e) {
            throw e;
        } finally {
            // free keyAddress
            KAERSACipher.nativeFreeKey(keyAddress);
        }
        return sigBytes;
    }

    // verify the data and return the result. See JCA doc
    @Override
    protected boolean engineVerify(byte[] sigBytes) throws SignatureException {
        if (publicKey == null) {
            throw new SignatureException("Missing public key");
        }

        if (sigBytes.length != RSACore.getByteLength(publicKey)) {
            throw new SignatureException("Signature length not correct: got " +
                    sigBytes.length + " but was expecting " +
                    RSACore.getByteLength(publicKey));
        }
        String kaeDigestName = KAEUtils.getKAEDigestName(this.digestAlgorithm);
        byte[] digest = getDigestValue();
        long keyAddress = KAERSACipher.nativeCreateRSAPublicKey(publicKey.getModulus().toByteArray(),
                publicKey.getPublicExponent().toByteArray());

        boolean verify;
        try {
            verify = KAERSASignatureNative.rsaVerify(keyAddress,
                    kaeDigestName, digest, KAERSAPaddingType.PKCS1Padding.getId(), sigBytes);
        } catch (SignatureException e) {
            throw e;
        } catch (BadPaddingException e) {
            // occurs if the app has used the wrong RSA public key
            // or if sigBytes is invalid or sourceBytes is invalid
            // return false rather than propagating the exception for
            // compatibility/ease of use
            return false;
        } finally {
            // free keyAddress
            KAERSACipher.nativeFreeKey(keyAddress);
        }
        return verify;
    }

    /**
     * Encode the digest, return the to-be-signed data.
     * Also used by the PKCS#11 provider.
     */
    public static byte[] encodeSignature(ObjectIdentifier oid, byte[] digest) throws IOException {
        DerOutputStream out = new DerOutputStream();
        new AlgorithmId(oid).encode(out);
        out.putOctetString(digest);
        DerValue result =
                new DerValue(DerValue.tag_Sequence, out.toByteArray());
        return result.toByteArray();
    }

    // set parameter, not supported. See JCA doc
    @Deprecated
    @Override
    protected void engineSetParameter(String param, Object value) throws InvalidParameterException {
        throw new UnsupportedOperationException("setParameter() not supported");
    }

    // See JCA doc
    @Override
    protected void engineSetParameter(AlgorithmParameterSpec params) throws InvalidAlgorithmParameterException {
        if (params != null) {
            throw new InvalidAlgorithmParameterException("No parameters accepted");
        }
    }

    // get parameter, not supported. See JCA doc
    @Deprecated
    @Override
    protected Object engineGetParameter(String param) throws InvalidParameterException {
        throw new UnsupportedOperationException("getParameter() not supported");
    }

    // See JCA doc
    @Override
    protected AlgorithmParameters engineGetParameters() {
        return null;
    }

    // Nested class for MD5withRSA signatures
    public static final class MD5withRSA extends KAERSASignature {
        public MD5withRSA() {
            super("MD5", AlgorithmId.MD5_oid, 10);
        }
    }

    // Nested class for SHA1withRSA signatures
    public static final class SHA1withRSA extends KAERSASignature {
        public SHA1withRSA() {
            super("SHA-1", AlgorithmId.SHA_oid, 7);
        }
    }

    // Nested class for SHA224withRSA signatures
    public static final class SHA224withRSA extends KAERSASignature {
        public SHA224withRSA() {
            super("SHA-224", AlgorithmId.SHA224_oid, 11);
        }
    }

    // Nested class for SHA256withRSA signatures
    public static final class SHA256withRSA extends KAERSASignature {
        public SHA256withRSA() {
            super("SHA-256", AlgorithmId.SHA256_oid, 11);
        }
    }

    // Nested class for SHA384withRSA signatures
    public static final class SHA384withRSA extends KAERSASignature {
        public SHA384withRSA() {
            super("SHA-384", AlgorithmId.SHA384_oid, 11);
        }
    }

    // Nested class for SHA512withRSA signatures
    public static final class SHA512withRSA extends KAERSASignature {
        public SHA512withRSA() {
            super("SHA-512", AlgorithmId.SHA512_oid, 11);
        }
    }
}
