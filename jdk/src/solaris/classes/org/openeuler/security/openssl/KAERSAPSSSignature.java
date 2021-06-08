/*
 * Copyright (c) 2018, 2020, Oracle and/or its affiliates. All rights reserved.
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
import java.lang.reflect.Constructor;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.nio.ByteBuffer;

import java.security.*;
import java.security.spec.AlgorithmParameterSpec;
import java.security.spec.PSSParameterSpec;
import java.security.spec.MGF1ParameterSpec;
import java.security.interfaces.*;

import java.util.Arrays;
import java.util.HashSet;
import java.util.Locale;
import java.util.Set;

import sun.security.rsa.PSSParameters;
import sun.security.rsa.RSACore;
import sun.security.jca.JCAUtil;

import javax.crypto.BadPaddingException;

/**
 * PKCS#1 v2.2 RSASSA-PSS signatures with various message digest algorithms.
 * RSASSA-PSS implementation takes the message digest algorithm, MGF algorithm,
 * and salt length values through the required signature PSS parameters.
 * We support SHA-1, SHA-224, SHA-256, SHA-384, SHA-512.
 * The Openssl does not support rsa signatures with SHA-512/224 and SHA-512/256 as the digest algorithm,
 * so we have not implemented them.
 * The Openssl does not support non-CRT private key , when signing with a non-CRT private key, we use the sun sign.
 */
public class KAERSAPSSSignature extends SignatureSpi {
    // openssl unsupport rsa sign with digest algorithm
    private static final Set<String> UNSUPPORTED_DIGEST_ALGORITHM = new HashSet<>(
            Arrays.asList("SHA-512/224", "SHA-512/256"));

    private static Method generateAndXorMethod;

    private static Constructor mgf1Constructor;

    private static final byte[] EIGHT_BYTES_OF_ZEROS = new byte[8];

    private static Exception exception;

    static {
        AccessController.doPrivileged(new PrivilegedAction<Void>() {
            @Override
            public Void run() {
                try {
                    Class<?> mgf1Class = Class.forName("sun.security.rsa.MGF1");
                    generateAndXorMethod = mgf1Class.getDeclaredMethod("generateAndXor",
                            byte[].class, int.class, int.class, int.class, byte[].class, int.class);
                    generateAndXorMethod.setAccessible(true);
                    mgf1Constructor = mgf1Class.getDeclaredConstructor(String.class);
                    mgf1Constructor.setAccessible(true);
                } catch (ClassNotFoundException | NoSuchMethodException e) {
                    exception = e;
                }
                return null;
            }
        });
    }

    // message digest implementation we use for hashing the data
    private MessageDigest md;

    // flag indicating whether the digest is reset
    private boolean digestReset = true;

    // private key, if initialized for signing
    private RSAPrivateKey privKey = null;

    // public key, if initialized for verifying
    private RSAPublicKey pubKey = null;

    // PSS parameters from signatures and keys respectively
    private PSSParameterSpec sigParams = null;

    // PRNG used to generate salt bytes if none given
    private SecureRandom random;

    /**
     * Construct a new RSAPSSSignatur with arbitrary digest algorithm
     */
    public KAERSAPSSSignature() throws SignatureException {
        if (exception != null) {
            throw new SignatureException(exception.getMessage());
        }
        this.md = null;
    }

    // initialize for verification. See JCA doc
    @Override
    protected void engineInitVerify(PublicKey publicKey) throws InvalidKeyException {
        if (!(publicKey instanceof RSAPublicKey)) {
            throw new InvalidKeyException("key must be RSAPublicKey");
        }
        this.pubKey = (RSAPublicKey) isValid((RSAKey) publicKey);
        this.privKey = null;
        resetDigest();
    }

    // initialize for signing. See JCA doc
    @Override
    protected void engineInitSign(PrivateKey privateKey) throws InvalidKeyException {
        engineInitSign(privateKey, null);
    }

    // initialize for signing. See JCA doc
    @Override
    protected void engineInitSign(PrivateKey privateKey, SecureRandom random) throws InvalidKeyException {
        if (!(privateKey instanceof RSAPrivateKey)) {
            throw new InvalidKeyException("key must be RSAPrivateKey");
        }
        this.privKey = (RSAPrivateKey) isValid((RSAKey) privateKey);
        this.pubKey = null;
        this.random =
                (random == null ? JCAUtil.getSecureRandom() : random);
        resetDigest();
    }

    /**
     * Utility method for checking the key PSS parameters against signature
     * PSS parameters.
     * Returns false if any of the digest/MGF algorithms and trailerField
     * values does not match or if the salt length in key parameters is
     * larger than the value in signature parameters.
     */
    private static boolean isCompatible(AlgorithmParameterSpec keyParams, PSSParameterSpec sigParams) {
        if (keyParams == null) {
            // key with null PSS parameters means no restriction
            return true;
        }
        if (!(keyParams instanceof PSSParameterSpec)) {
            return false;
        }
        // nothing to compare yet, defer the check to when sigParams is set
        if (sigParams == null) {
            return true;
        }
        PSSParameterSpec pssKeyParams = (PSSParameterSpec) keyParams;
        // first check the salt length requirement
        if (pssKeyParams.getSaltLength() > sigParams.getSaltLength()) {
            return false;
        }

        // compare equality of the rest of fields based on DER encoding
        PSSParameterSpec keyParams2 =
                new PSSParameterSpec(pssKeyParams.getDigestAlgorithm(),
                        pssKeyParams.getMGFAlgorithm(),
                        pssKeyParams.getMGFParameters(),
                        sigParams.getSaltLength(),
                        pssKeyParams.getTrailerField());

        // skip the JCA overhead
        try {
            byte[] encoded = PSSParameters.getEncoded(keyParams2);
            byte[] encoded2 = PSSParameters.getEncoded(sigParams);
            return Arrays.equals(encoded, encoded2);
        } catch (IOException e) {
            return false;
        }
    }

    /**
     * Validate the specified RSAKey and its associated parameters against
     * internal signature parameters.
     */
    private RSAKey isValid(RSAKey rsaKey) throws InvalidKeyException {
        try {
            AlgorithmParameterSpec keyParams = rsaKey.getParams();
            // validate key parameters
            if (!isCompatible(rsaKey.getParams(), this.sigParams)) {
                throw new InvalidKeyException
                        ("Key contains incompatible PSS parameter values");
            }
            // validate key length
            if (this.sigParams != null) {
                Integer hLen =
                        KAEUtils.getDigestLength(this.sigParams.getDigestAlgorithm());
                if (hLen == null) {
                    throw new ProviderException("Unsupported digest algo: " +
                            this.sigParams.getDigestAlgorithm());
                }
                checkKeyLength(rsaKey, hLen, this.sigParams.getSaltLength());
            }
            return rsaKey;
        } catch (SignatureException e) {
            throw new InvalidKeyException(e);
        }
    }

    /**
     * Validate the specified Signature PSS parameters.
     */
    private PSSParameterSpec validateSigParams(AlgorithmParameterSpec p) throws InvalidAlgorithmParameterException {
        if (p == null) {
            throw new InvalidAlgorithmParameterException
                    ("Parameters cannot be null");
        }
        if (!(p instanceof PSSParameterSpec)) {
            throw new InvalidAlgorithmParameterException
                    ("parameters must be type PSSParameterSpec");
        }
        // no need to validate again if same as current signature parameters
        PSSParameterSpec params = (PSSParameterSpec) p;
        if (params == this.sigParams) {
            return params;
        }

        RSAKey key = (this.privKey == null ? this.pubKey : this.privKey);
        // check against keyParams if set
        if (key != null) {
            if (!isCompatible(key.getParams(), params)) {
                throw new InvalidAlgorithmParameterException
                        ("Signature parameters does not match key parameters");
            }
        }
        // now sanity check the parameter values
        if (!(params.getMGFAlgorithm().equalsIgnoreCase("MGF1"))) {
            throw new InvalidAlgorithmParameterException("Only supports MGF1");
        }
        if (params.getTrailerField() != PSSParameterSpec.TRAILER_FIELD_BC) {
            throw new InvalidAlgorithmParameterException("Only supports TrailerFieldBC(1)");
        }
        String digestAlgo = params.getDigestAlgorithm();
        // check key length again
        if (key != null) {
            try {
                int hLen = KAEUtils.getDigestLength(digestAlgo);
                checkKeyLength(key, hLen, params.getSaltLength());
            } catch (SignatureException e) {
                throw new InvalidAlgorithmParameterException(e);
            }
        }
        return params;
    }

    /**
     * Ensure the object is initialized with key and parameters and
     * reset digest
     */
    private void ensureInit() throws SignatureException {
        RSAKey key = (this.privKey == null ? this.pubKey : this.privKey);
        if (key == null) {
            throw new SignatureException("Missing key");
        }
        if (this.sigParams == null) {
            // Parameters are required for signature verification
            throw new SignatureException
                    ("Parameters required for RSASSA-PSS signatures");
        }
    }

    /**
     * Utility method for checking key length against digest length and
     * salt length
     */
    private static void checkKeyLength(RSAKey key, int digestLen, int saltLen) throws SignatureException {
        if (key != null) {
            int keyLength = getKeyLengthInBits(key) >> 3;
            int minLength = Math.addExact(Math.addExact(digestLen, saltLen), 2);
            if (keyLength < minLength) {
                throw new SignatureException
                        ("Key is too short, need min " + minLength);
            }
        }
    }

    /**
     * Reset the message digest if it is not already reset.
     */
    private void resetDigest() {
        if (digestReset == false) {
            this.md.reset();
            digestReset = true;
        }
    }

    /**
     * Return the message digest value.
     */
    private byte[] getDigestValue() {
        digestReset = true;
        return this.md.digest();
    }

    // update the signature with the plaintext data. See JCA doc
    @Override
    protected void engineUpdate(byte b) throws SignatureException {
        ensureInit();
        this.md.update(b);
        digestReset = false;
    }

    // update the signature with the plaintext data. See JCA doc
    @Override
    protected void engineUpdate(byte[] b, int off, int len) throws SignatureException {
        ensureInit();
        this.md.update(b, off, len);
        digestReset = false;
    }

    // update the signature with the plaintext data. See JCA doc
    @Override
    protected void engineUpdate(ByteBuffer b) {
        try {
            ensureInit();
        } catch (SignatureException se) {
            // hack for working around API bug
            throw new RuntimeException(se.getMessage());
        }
        this.md.update(b);
        digestReset = false;
    }

    // determine whether the digest is valid
    private boolean isValidDigest() {
        String digestName = this.md.getAlgorithm();
        if (UNSUPPORTED_DIGEST_ALGORITHM.contains(digestName.toUpperCase(Locale.ROOT))) {
            return false;
        }
        AlgorithmParameterSpec mgfParams = this.sigParams.getMGFParameters();
        String mgfDigestName = "";
        if (mgfParams != null) {
            mgfDigestName =
                    ((MGF1ParameterSpec) mgfParams).getDigestAlgorithm();
        }
        return !UNSUPPORTED_DIGEST_ALGORITHM.contains(mgfDigestName.toUpperCase(Locale.ROOT));
    }

    // determine whether use kae sign
    private boolean useKaeSign() {
        return (privKey instanceof RSAPrivateCrtKey) && isValidDigest();
    }

    // sun sign
    private byte[] sunSign(byte[] mHash) throws SignatureException {
        try {
            byte[] encoded = encodeSignature(mHash);
            return RSACore.rsa(encoded, privKey, true);
        } catch (GeneralSecurityException e) {
            throw new SignatureException("Could not sign data", e);
        } catch (IOException e) {
            throw new SignatureException("Could not encode data", e);
        }
    }

    // kae sign
    private byte[] kaeSign(byte[] mHash) throws SignatureException {
        String kaeDigestName = KAEUtils.getKAEDigestName(this.sigParams.getDigestAlgorithm());
        String mgfDigestName = kaeDigestName;
        AlgorithmParameterSpec mgfParams = this.sigParams.getMGFParameters();
        if (mgfParams != null) {
            mgfDigestName =
                    ((MGF1ParameterSpec) mgfParams).getDigestAlgorithm();
        }
        String mgf1DigestName = KAEUtils.getKAEDigestName(mgfDigestName);

        RSAPrivateCrtKey privateCrtKey = (RSAPrivateCrtKey) privKey;
        long keyAddress = KAERSACipher.nativeCreateRSAPrivateCrtKey(
                privateCrtKey.getModulus().toByteArray(),
                privateCrtKey.getPublicExponent().toByteArray(),
                privateCrtKey.getPrivateExponent().toByteArray(),
                privateCrtKey.getPrimeP().toByteArray(),
                privateCrtKey.getPrimeQ().toByteArray(),
                privateCrtKey.getPrimeExponentP().toByteArray(),
                privateCrtKey.getPrimeExponentQ().toByteArray(),
                privateCrtKey.getCrtCoefficient().toByteArray());
        byte[] bytes;
        try {
            bytes = KAERSASignatureNative.pssSign(keyAddress, kaeDigestName, mHash,
                    KAERSAPaddingType.PKCS1PssPadding.getId(),
                    mgf1DigestName, this.sigParams.getSaltLength());
        } catch (SignatureException e) {
            throw e;
        } finally {
            KAERSACipher.nativeFreeKey(keyAddress);
        }
        return bytes;
    }

    // sign the data and return the signature. See JCA doc
    @Override
    protected byte[] engineSign() throws SignatureException {
        ensureInit();
        byte[] mHash = getDigestValue();
        if (useKaeSign()) {
            return kaeSign(mHash);
        }
        return sunSign(mHash);
    }

    // determine whether use kae verify
    private boolean useKaeVerify() {
        return isValidDigest();
    }

    // sun verify
    private boolean sunVerify(byte[] mHash, byte[] sigBytes) throws SignatureException {
        try {
            byte[] decrypted = RSACore.rsa(sigBytes, this.pubKey);
            return decodeSignature(mHash, decrypted);
        } catch (javax.crypto.BadPaddingException e) {
            // occurs if the app has used the wrong RSA public key
            // or if sigBytes is invalid
            // return false rather than propagating the exception for
            // compatibility/ease of use
            return false;
        } catch (IOException e) {
            throw new SignatureException("Signature encoding error", e);
        } finally {
            resetDigest();
        }
    }

    // kae verify
    private boolean kaeVerify(byte[] mHash, byte[] sigBytes) throws SignatureException {
        String kaeDigestName = KAEUtils.getKAEDigestName(this.sigParams.getDigestAlgorithm());
        String mgfDigestName = kaeDigestName;
        AlgorithmParameterSpec mgfParams = this.sigParams.getMGFParameters();
        if (mgfParams != null) {
            mgfDigestName =
                    ((MGF1ParameterSpec) mgfParams).getDigestAlgorithm();
        }
        String mgf1KaeDigestName = KAEUtils.getKAEDigestName(mgfDigestName);

        long keyAddress = KAERSACipher.nativeCreateRSAPublicKey(this.pubKey.getModulus().toByteArray(),
                this.pubKey.getPublicExponent().toByteArray());
        boolean verify;
        try {
            verify = KAERSASignatureNative.pssVerify(keyAddress, kaeDigestName, mHash,
                    KAERSAPaddingType.PKCS1PssPadding.getId(), mgf1KaeDigestName,
                    this.sigParams.getSaltLength(), sigBytes);
        } catch (SignatureException e) {
            throw e;
        } catch (BadPaddingException e) {
            // occurs if the app has used the wrong RSA public key
            // or if sigBytes is invalid or sourceBytes is invalid
            // return false rather than propagating the exception for
            // compatibility/ease of use
            return false;
        } finally {
            resetDigest();
            KAERSACipher.nativeFreeKey(keyAddress);
        }
        return verify;
    }

    // verify the data and return the result. See JCA doc
    // should be reset to the state after engineInitVerify call.
    @Override
    protected boolean engineVerify(byte[] sigBytes) throws SignatureException {
        ensureInit();
        if (sigBytes.length != RSACore.getByteLength(this.pubKey)) {
            throw new SignatureException("Signature length not correct: got " +
                    sigBytes.length + " but was expecting " +
                    RSACore.getByteLength(this.pubKey));
        }
        byte[] mHash = getDigestValue();
        if (useKaeVerify()) {
            return kaeVerify(mHash, sigBytes);
        }
        return sunVerify(mHash, sigBytes);
    }

    // return the modulus length in bits
    private static int getKeyLengthInBits(RSAKey k) {
        if (k != null) {
            return k.getModulus().bitLength();
        }
        return -1;
    }

    /**
     * Encode the digest 'mHash', return the to-be-signed data.
     * Also used by the PKCS#11 provider.
     */
    private byte[] encodeSignature(byte[] mHash) throws IOException, DigestException {
        AlgorithmParameterSpec mgfParams = this.sigParams.getMGFParameters();
        String mgfDigestAlgo;
        if (mgfParams != null) {
            mgfDigestAlgo =
                    ((MGF1ParameterSpec) mgfParams).getDigestAlgorithm();
        } else {
            mgfDigestAlgo = this.md.getAlgorithm();
        }
        try {
            int emBits = getKeyLengthInBits(this.privKey) - 1;
            int emLen = (emBits + 7) >> 3;
            int hLen = this.md.getDigestLength();
            int dbLen = emLen - hLen - 1;
            int sLen = this.sigParams.getSaltLength();

            // maps DB into the corresponding region of EM and
            // stores its bytes directly into EM
            byte[] em = new byte[emLen];

            // step7 and some of step8
            em[dbLen - sLen - 1] = (byte) 1; // set DB's padding2 into EM
            em[em.length - 1] = (byte) 0xBC; // set trailer field of EM

            if (!digestReset) {
                throw new ProviderException("Digest should be reset");
            }
            // step5: generates M' using padding1, mHash, and salt
            this.md.update(EIGHT_BYTES_OF_ZEROS);
            digestReset = false; // mark digest as it now has data
            this.md.update(mHash);
            if (sLen != 0) {
                // step4: generate random salt
                byte[] salt = new byte[sLen];
                this.random.nextBytes(salt);
                this.md.update(salt);

                // step8: set DB's salt into EM
                System.arraycopy(salt, 0, em, dbLen - sLen, sLen);
            }
            // step6: generate H using M'
            this.md.digest(em, dbLen, hLen); // set H field of EM
            digestReset = true;

            // step7 and 8 are already covered by the code which setting up
            // EM as above

            // step9 and 10: feed H into MGF and xor with DB in EM
            Object mgf1 = mgf1Constructor.newInstance(mgfDigestAlgo);
            generateAndXorMethod.invoke(mgf1, em, dbLen, hLen, dbLen, em, 0);
            // step11: set the leftmost (8emLen - emBits) bits of the leftmost
            // octet to 0
            int numZeroBits = (emLen << 3) - emBits;
            if (numZeroBits != 0) {
                byte mask = (byte) (0xff >>> numZeroBits);
                em[0] = (byte) (em[0] & mask);
            }

            // step12: em should now holds maskedDB || hash h || 0xBC
            return em;
        } catch (IllegalAccessException | InstantiationException | InvocationTargetException e) {
            throw new IOException(e.toString());
        }
    }

    private String getMgfDigestAlgo() {
        String mgfDigestAlgo;
        AlgorithmParameterSpec mgfParams = this.sigParams.getMGFParameters();
        if (mgfParams != null) {
            mgfDigestAlgo =
                    ((MGF1ParameterSpec) mgfParams).getDigestAlgorithm();
        } else {
            mgfDigestAlgo = this.md.getAlgorithm();
        }
        return mgfDigestAlgo;
    }

    private void generateAndXor(String mgfDigestAlgo, byte[] em, int dbLen, int hLen) throws IOException {
        Object mgf1;
        try {
            mgf1 = mgf1Constructor.newInstance(mgfDigestAlgo);
            generateAndXorMethod.invoke(mgf1, em, dbLen, hLen, dbLen, em, 0);
        } catch (InstantiationException | IllegalAccessException | InvocationTargetException e) {
            throw new IOException(e.toString());
        }
    }

    /**
     * Decode the signature data. Verify that the object identifier matches
     * and return the message digest.
     */
    private boolean decodeSignature(byte[] mHash, byte[] em) throws IOException {
        int hLen = mHash.length;
        int sLen = this.sigParams.getSaltLength();
        int emLen = em.length;
        int emBits = getKeyLengthInBits(this.pubKey) - 1;

        // step3
        if (emLen < (hLen + sLen + 2)) {
            return false;
        }

        // step4
        if (em[emLen - 1] != (byte) 0xBC) {
            return false;
        }

        // step6: check if the leftmost (8emLen - emBits) bits of the leftmost
        // octet are 0
        int numZeroBits = (emLen << 3) - emBits;
        if (numZeroBits != 0) {
            byte mask = (byte) (0xff << (8 - numZeroBits));
            if ((em[0] & mask) != 0) {
                return false;
            }
        }

        // step 7 and 8
        int dbLen = emLen - hLen - 1;

        // generateAndXor
        String mgfDigestAlgo = getMgfDigestAlgo();
        generateAndXor(mgfDigestAlgo, em, dbLen, hLen);

        // step9: set the leftmost (8emLen - emBits) bits of the leftmost
        // octet to 0
        if (numZeroBits != 0) {
            byte mask = (byte) (0xff >>> numZeroBits);
            em[0] = (byte) (em[0] & mask);
        }

        // step10
        int index = 0;
        for (; index < dbLen - sLen - 1; index++) {
            if (em[index] != 0) {
                return false;
            }
        }
        if (em[index] != 0x01) {
            return false;
        }
        // step12 and 13
        this.md.update(EIGHT_BYTES_OF_ZEROS);
        digestReset = false;
        this.md.update(mHash);
        if (sLen > 0) {
            this.md.update(em, (dbLen - sLen), sLen);
        }
        byte[] digest2 = this.md.digest();
        digestReset = true;

        // step14
        byte[] digestInEM = Arrays.copyOfRange(em, dbLen, emLen - 1);
        return MessageDigest.isEqual(digest2, digestInEM);
    }

    // set parameter, not supported. See JCA doc
    @Deprecated
    @Override
    protected void engineSetParameter(String param, Object value) throws InvalidParameterException {
        throw new UnsupportedOperationException("setParameter() not supported");
    }

    @Override
    protected void engineSetParameter(AlgorithmParameterSpec params) throws InvalidAlgorithmParameterException {
        this.sigParams = validateSigParams(params);
        // disallow changing parameters when digest has been used
        if (!digestReset) {
            throw new ProviderException
                    ("Cannot set parameters during operations");
        }
        String newHashAlg = this.sigParams.getDigestAlgorithm();
        // re-allocate md if not yet assigned or algorithm changed
        if ((this.md == null) ||
                !(this.md.getAlgorithm().equalsIgnoreCase(newHashAlg))) {
            try {
                this.md = MessageDigest.getInstance(newHashAlg);
            } catch (NoSuchAlgorithmException nsae) {
                // should not happen as we pick default digest algorithm
                throw new InvalidAlgorithmParameterException("Unsupported digest algorithm " + newHashAlg, nsae);
            }
        }
    }

    // get parameter, not supported. See JCA doc
    @Deprecated
    @Override
    protected Object engineGetParameter(String param) throws InvalidParameterException {
        throw new UnsupportedOperationException("getParameter() not supported");
    }

    @Override
    protected AlgorithmParameters engineGetParameters() {
        AlgorithmParameters ap = null;
        if (this.sigParams != null) {
            try {
                ap = AlgorithmParameters.getInstance("RSASSA-PSS");
                ap.init(this.sigParams);
            } catch (GeneralSecurityException gse) {
                throw new ProviderException(gse.getMessage());
            }
        }
        return ap;
    }
}

