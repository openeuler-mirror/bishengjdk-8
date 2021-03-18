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

import sun.security.internal.spec.TlsRsaPremasterSecretParameterSpec;
import sun.security.jca.Providers;
import sun.security.rsa.RSACore;
import sun.security.rsa.RSAKeyFactory;
import sun.security.rsa.RSAPadding;
import sun.security.util.KeyUtil;

import javax.crypto.*;
import javax.crypto.spec.OAEPParameterSpec;
import javax.crypto.spec.PSource;
import java.lang.ref.PhantomReference;
import java.lang.ref.ReferenceQueue;
import java.security.*;
import java.security.interfaces.RSAKey;
import java.security.interfaces.RSAPrivateCrtKey;
import java.security.interfaces.RSAPrivateKey;
import java.security.interfaces.RSAPublicKey;
import java.security.spec.AlgorithmParameterSpec;
import java.security.spec.InvalidParameterSpecException;
import java.security.spec.MGF1ParameterSpec;
import java.util.Arrays;
import java.util.Locale;
import java.util.Set;
import java.util.concurrent.ConcurrentSkipListSet;


/**
 * RSA cipher implementation. Supports RSA en/decryption and signing/verifying
 * using both PKCS#1 v1.5 and OAEP (v2.2) paddings and without padding (raw RSA).
 * Note that raw RSA is supported mostly for completeness and should only be
 * used in rare cases.
 * <p>
 * Objects should be instantiated by calling Cipher.getInstance() using the
 * following algorithm names:
 * . "RSA/ECB/PKCS1Padding" (or "RSA") for PKCS#1 v1.5 padding.
 * . "RSA/ECB/OAEPwith<hash>andMGF1Padding" (or "RSA/ECB/OAEPPadding") for
 * PKCS#1 v2.2 padding.
 * . "RSA/ECB/NoPadding" for rsa RSA.
 * <p>
 * We only do one RSA operation per doFinal() call. If the application passes
 * more data via calls to update() or doFinal(), we throw an
 * IllegalBlockSizeException when doFinal() is called (see JCE API spec).
 * Bulk encryption using RSA does not make sense and is not standardized.
 * <p>
 * Note: RSA keys should be at least 512 bits long
 */
public final class KAERSACipher extends CipherSpi {
    // constant for an empty byte array
    private final static byte[] B0 = new byte[0];

    // mode constant for public key encryption
    private final static int MODE_ENCRYPT = 1;

    // mode constant for private key decryption
    private final static int MODE_DECRYPT = 2;

    // mode constant for private key encryption (signing)
    private final static int MODE_SIGN = 3;

    // mode constant for public key decryption (verifying)
    private final static int MODE_VERIFY = 4;

    // current mode, one of MODE_* above. Set when init() is called
    private int mode;

    // active padding type, one of PAD_* above. Set by setPadding()
    private KAERSAPaddingType paddingType;

    // padding object
    private RSAPadding padding;

    // cipher parameter for OAEP padding and TLS RSA premaster secret
    private AlgorithmParameterSpec spec = null;

    // buffer for the data
    private byte[] buffer;
    // offset into the buffer (number of bytes buffered)
    private int bufOfs;

    // size of the output
    private int outputSize;

    // hash algorithm for OAEP
    private String oaepHashAlgorithm = "SHA-1";

    // the source of randomness
    private SecureRandom random;

    private RSAKey rsaKey;

    // rsa key holder
    private KAERSAKeyHolder rsaKeyHolder;


    public KAERSACipher() {
        paddingType = KAERSAPaddingType.PKCS1Padding;
    }

    // modes do not make sense for RSA, but allow ECB
    // see JCE spec
    @Override
    protected void engineSetMode(String mode) throws NoSuchAlgorithmException {
        if (!mode.equalsIgnoreCase("ECB")) {
            throw new NoSuchAlgorithmException("Unsupported mode " + mode);
        }
    }

    // set the padding type
    // see JCE spec
    @Override
    protected void engineSetPadding(String paddingName)
            throws NoSuchPaddingException {
        if (KAERSAPaddingType.NoPadding.getName().equalsIgnoreCase(paddingName)) {
            paddingType = KAERSAPaddingType.NoPadding;
        } else if (KAERSAPaddingType.PKCS1Padding.getName().equalsIgnoreCase(paddingName)) {
            paddingType = KAERSAPaddingType.PKCS1Padding;
        } else {
            String lowerPadding = paddingName.toLowerCase(Locale.ENGLISH);
            if ("oaeppadding".equals(lowerPadding)) {
                paddingType = KAERSAPaddingType.OAEP;
            } else if (lowerPadding.startsWith("oaepwith") &&
                    lowerPadding.endsWith("andmgf1padding")) {
                paddingType = KAERSAPaddingType.OAEP;
                // "oaepwith" length is 8
                // "andmgf1padding" length is 14
                oaepHashAlgorithm =
                        paddingName.substring(8, paddingName.length() - 14);
                // check if MessageDigest appears to be available
                // avoid getInstance() call here
                if (Providers.getProviderList().getService
                        ("MessageDigest", oaepHashAlgorithm) == null) {
                    throw new NoSuchPaddingException
                            ("MessageDigest not available for " + paddingName);
                }
            } else {
                throw new NoSuchPaddingException
                        ("Padding " + paddingName + " not supported");
            }
        }
    }

    // return 0 as block size, we are not a block cipher
    // see JCE spec
    @Override
    protected int engineGetBlockSize() {
        return 0;
    }

    // return the output size
    // see JCE spec
    @Override
    protected int engineGetOutputSize(int inputLen) {
        return outputSize;
    }

    // no iv, return null
    // see JCE spec
    @Override
    protected byte[] engineGetIV() {
        return null;
    }

    // see JCE spec
    @Override
    protected AlgorithmParameters engineGetParameters() {
        if (spec != null && spec instanceof OAEPParameterSpec) {
            try {
                AlgorithmParameters params =
                        AlgorithmParameters.getInstance("OAEP");
                params.init(spec);
                return params;
            } catch (NoSuchAlgorithmException nsae) {
                // should never happen
                throw new RuntimeException("Cannot find OAEP " +
                        " AlgorithmParameters implementation in SunJCE provider");
            } catch (InvalidParameterSpecException ipse) {
                // should never happen
                throw new RuntimeException("OAEPParameterSpec not supported");
            }
        } else {
            return null;
        }
    }

    // see JCE spec
    @Override
    protected void engineInit(int opmode, Key key, SecureRandom random)
            throws InvalidKeyException {
        try {
            init(opmode, key, random, null);
        } catch (InvalidAlgorithmParameterException iape) {
            // never thrown when null parameters are used;
            // but re-throw it just in case
            throw new InvalidKeyException("Wrong parameters", iape);
        }
    }

    // see JCE spec
    @Override
    protected void engineInit(int opmode, Key key, AlgorithmParameterSpec params, SecureRandom random)
            throws InvalidKeyException, InvalidAlgorithmParameterException {
        init(opmode, key, random, params);
    }

    // see JCE spec
    @Override
    protected void engineInit(int opmode, Key key, AlgorithmParameters params, SecureRandom random)
            throws InvalidKeyException, InvalidAlgorithmParameterException {
        if (params == null) {
            init(opmode, key, random, null);
        } else {
            try {
                OAEPParameterSpec oaepParameterSpec =
                        params.getParameterSpec(OAEPParameterSpec.class);
                init(opmode, key, random, oaepParameterSpec);
            } catch (InvalidParameterSpecException ipse) {
                InvalidAlgorithmParameterException iape =
                        new InvalidAlgorithmParameterException("Wrong parameter");
                iape.initCause(ipse);
                throw iape;
            }
        }
    }

    // check TlsRsaPremasterSecretParameterSpec
    private void checkTlsRsaPremasterSecretParameterSpec(AlgorithmParameterSpec params)
            throws InvalidAlgorithmParameterException {
        if (!(params instanceof TlsRsaPremasterSecretParameterSpec)) {
            throw new InvalidAlgorithmParameterException(
                    "Parameters not supported");
        }
    }

    // check OAEPParameterSpec
    private void checkOAEPParameterSpec(AlgorithmParameterSpec params)
            throws InvalidAlgorithmParameterException {
        if (!(params instanceof OAEPParameterSpec)) {
            throw new InvalidAlgorithmParameterException
                    ("Wrong Parameters for OAEP Padding");
        }

        // check MGF algorithm
        OAEPParameterSpec oaepParameterSpec = (OAEPParameterSpec) params;
        String mgfName = oaepParameterSpec.getMGFAlgorithm();
        if (!mgfName.equalsIgnoreCase("MGF1")) {
            throw new InvalidAlgorithmParameterException
                    ("Unsupported MGF algo: " + mgfName);
        }

        // check PSource algorithm
        PSource pSource = oaepParameterSpec.getPSource();
        String pSourceAlgorithm = pSource.getAlgorithm();
        if (!pSourceAlgorithm.equalsIgnoreCase("PSpecified")) {
            throw new InvalidAlgorithmParameterException
                    ("Unsupported pSource algo: " + pSourceAlgorithm);
        }
    }

    // compute OAEP data buffer length
    private int getOAEPBufferLen(int outputSize, OAEPParameterSpec oaepParameterSpec, boolean encrypt)
            throws InvalidKeyException {
        if (!encrypt) {
            return outputSize;
        }
        String mdName = oaepParameterSpec.getDigestAlgorithm();
        String mgfMdName = ((MGF1ParameterSpec) oaepParameterSpec.getMGFParameters())
                .getDigestAlgorithm();
        int digestLen = KAEUtils.getDigestLength(mdName);
        int bufferLen = outputSize - 2 - 2 * digestLen;
        if (bufferLen < 0) {
            throw new InvalidKeyException
                    ("Key is too short for encryption using OAEPPadding" +
                            " with " + mdName + " and MGF1" + mgfMdName);
        }
        return bufferLen;
    }

    // non-CRT private key, use the jdk soft calculation.
    private boolean useJdkSoftCalculation() {
        return (rsaKey instanceof RSAPrivateKey) && !(rsaKey instanceof RSAPrivateCrtKey);
    }

    // get the rsa padding
    private RSAPadding getRSAPadding(KAERSAPaddingType paddingType, int paddedSize,
                                     SecureRandom random, AlgorithmParameterSpec spec)
            throws InvalidKeyException, InvalidAlgorithmParameterException {
        RSAPadding rsaPadding;
        if (KAERSAPaddingType.NoPadding.equals(paddingType)) {
            rsaPadding = RSAPadding.getInstance(RSAPadding.PAD_NONE, paddedSize, random);
        } else if (KAERSAPaddingType.PKCS1Padding.equals(paddingType)) {
            int blockType = (mode <= MODE_DECRYPT) ? RSAPadding.PAD_BLOCKTYPE_2
                    : RSAPadding.PAD_BLOCKTYPE_1;
            rsaPadding = RSAPadding.getInstance(blockType, paddedSize, random);
        } else {
            rsaPadding = RSAPadding.getInstance(RSAPadding.PAD_OAEP_MGF1, paddedSize,
                    random, (OAEPParameterSpec) spec);
        }
        return rsaPadding;
    }

    private boolean isEncrypt(int opmode) throws InvalidKeyException {
        boolean encrypt;
        switch (opmode) {
            case Cipher.ENCRYPT_MODE:
            case Cipher.WRAP_MODE:
                encrypt = true;
                break;
            case Cipher.DECRYPT_MODE:
            case Cipher.UNWRAP_MODE:
                encrypt = false;
                break;
            default:
                throw new InvalidKeyException("Unknown mode: " + opmode);
        }
        return encrypt;
    }

    // initialize this cipher
    private void init(int opmode, Key key, SecureRandom random, AlgorithmParameterSpec params)
            throws InvalidKeyException, InvalidAlgorithmParameterException {
        // check the key, and convert to RSAKey
        rsaKey = RSAKeyFactory.toRSAKey(key);

        // init mode
        boolean encrypt = isEncrypt(opmode);
        if (key instanceof RSAPublicKey) {
            mode = encrypt ? MODE_ENCRYPT : MODE_VERIFY;
        } else {
            mode = encrypt ? MODE_SIGN : MODE_DECRYPT;
        }

        int bufferLen = RSACore.getByteLength(rsaKey.getModulus());
        outputSize = bufferLen;
        bufOfs = 0;
        if (KAERSAPaddingType.PKCS1Padding.equals(paddingType)) {
            if (params != null) {
                checkTlsRsaPremasterSecretParameterSpec(params);
                spec = params;
                this.random = random;   // for TLS RSA premaster secret
            }
            if (encrypt) {
                bufferLen -= 11;
            }
        } else if (KAERSAPaddingType.OAEP.equals(paddingType)) {
            if ((mode == MODE_SIGN) || (mode == MODE_VERIFY)) {
                throw new InvalidKeyException
                        ("OAEP cannot be used to sign or verify signatures");
            }
            if (params != null) {
                checkOAEPParameterSpec(params);
                spec = params;
            } else {
                spec = new OAEPParameterSpec(oaepHashAlgorithm, "MGF1",
                        MGF1ParameterSpec.SHA1, PSource.PSpecified.DEFAULT);
            }
            bufferLen = getOAEPBufferLen(bufferLen, (OAEPParameterSpec) spec, encrypt);
        }
        buffer = new byte[bufferLen];

        if (useJdkSoftCalculation()) {
            this.padding = getRSAPadding(paddingType, outputSize, random, spec);
        }
    }

    // internal update method
    private void update(byte[] in, int inOfs, int inLen) {
        if ((inLen == 0) || (in == null)) {
            return;
        }
        if (inLen > (buffer.length - bufOfs)) {
            bufOfs = buffer.length + 1;
            return;
        }
        System.arraycopy(in, inOfs, buffer, bufOfs, inLen);
        bufOfs += inLen;
    }

    // encrypt or decrypt for NoPadding or PKCS1Padding
    private int doCryptNotOAEPPadding(long keyAddress, byte[] input, byte[] output) throws BadPaddingException {
        int resultSize;
        switch (mode) {
            case MODE_SIGN:
                resultSize = nativeRSAPrivateEncrypt(keyAddress, input.length, input, output, paddingType.getId());
                break;
            case MODE_VERIFY:
                resultSize = nativeRSAPublicDecrypt(keyAddress, input.length, input, output, paddingType.getId());
                break;
            case MODE_ENCRYPT:
                resultSize = nativeRSAPublicEncrypt(keyAddress, input.length, input, output, paddingType.getId());
                break;
            case MODE_DECRYPT:
                resultSize = nativeRSAPrivateDecrypt(keyAddress, input.length, input, output, paddingType.getId());
                break;
            default:
                throw new AssertionError("Internal error");
        }
        return resultSize;
    }


    // encrypt or decrypt for OAEPPadding
    private int doCryptOAEPPadding(long keyAddress, byte[] input, byte[] output, OAEPParameterSpec oaepParameterSpec)
            throws BadPaddingException {
        // oaep digest algorithm
        String oaepMdAlgorithm = KAEUtils.getKAEDigestName(oaepParameterSpec.getDigestAlgorithm());
        // mgf1 digest algorithm
        MGF1ParameterSpec mgf1ParameterSpec = (MGF1ParameterSpec) oaepParameterSpec.getMGFParameters();
        String mgf1MdAlgorithm = KAEUtils.getKAEDigestName(mgf1ParameterSpec.getDigestAlgorithm());
        // label
        PSource pSource = oaepParameterSpec.getPSource();
        byte[] label = ((PSource.PSpecified) pSource).getValue();
        int resultSize;
        switch (mode) {
            case MODE_ENCRYPT:
                resultSize = nativeRSAEncryptOAEPPadding(keyAddress, input.length, input, output, paddingType.getId(),
                        oaepMdAlgorithm, mgf1MdAlgorithm, label);
                break;
            case MODE_DECRYPT:
                resultSize = nativeRSADecryptOAEPPadding(keyAddress, input.length, input, output, paddingType.getId(),
                        oaepMdAlgorithm, mgf1MdAlgorithm, label);
                break;
            default:
                throw new AssertionError("Internal error");
        }
        return resultSize;
    }

    // get input bytes
    private byte[] getInputBytes(byte[] buffer, int bufOfs, KAERSAPaddingType paddingType) {
        if (bufOfs == buffer.length) {
            return buffer;
        }

        // if padding type is NoPadding , data should move to end
        final byte[] input;
        if (KAERSAPaddingType.NoPadding.equals(paddingType)) {
            input = new byte[buffer.length];
            System.arraycopy(buffer, 0, input, buffer.length - bufOfs, bufOfs);
        } else {
            input = Arrays.copyOf(buffer, bufOfs);
        }
        return input;
    }

    // internal doFinal() method. Here we perform the actual RSA operation
    private byte[] doFinal() throws BadPaddingException, IllegalBlockSizeException {
        if (bufOfs > buffer.length) {
            throw new IllegalBlockSizeException("Data must not be longer "
                    + "than " + buffer.length + " bytes");
        }

        if (useJdkSoftCalculation()) {
            return doFinalForJdkSoftCalculation(padding);
        }

        // get input bytes
        final byte[] input = getInputBytes(buffer, bufOfs, paddingType);

        try {
            rsaKeyHolder = new KAERSAKeyHolder(this, rsaKey);
        } catch (InvalidKeyException e) {
            throw new RuntimeException(e.getMessage());
        }

        long keyAddress = rsaKeyHolder.keyAddress;
        byte[] output = new byte[outputSize];
        int cipherTextLength;
        try {
            if (KAERSAPaddingType.OAEP.equals(paddingType)) {
                // do crypt for OAEPPadding
                cipherTextLength = doCryptOAEPPadding(keyAddress, input, output, (OAEPParameterSpec) spec);
            } else {
                // do crypt for NoPadding or PKCS1Padding
                cipherTextLength = doCryptNotOAEPPadding(keyAddress, input, output);
            }

            // If mode is signing or verifying , and the length of the ciphertext is less than output length,
            // just keep output length ciphertext.
            if ((mode == MODE_VERIFY || mode == MODE_DECRYPT) && cipherTextLength != output.length) {
                output = Arrays.copyOf(output, cipherTextLength);
            }
        } finally {
            bufOfs = 0;
            resetKeyHolder();
        }
        return output;
    }

    private byte[] doFinalForJdkSoftCalculation(RSAPadding padding) throws BadPaddingException {
        try {
            byte[] data;
            switch (mode) {
                case MODE_SIGN:
                    data = padding.pad(buffer, 0, bufOfs);
                    return RSACore.rsa(data, (RSAPrivateKey) rsaKey, true);
                case MODE_DECRYPT:
                    byte[] decryptBuffer = RSACore.convert(buffer, 0, bufOfs);
                    data = RSACore.rsa(decryptBuffer, (RSAPrivateKey) rsaKey, false);
                    return padding.unpad(data);
                default:
                    throw new AssertionError("Internal error");
            }
        } finally {
            bufOfs = 0;
        }
    }

    // see JCE spec
    @Override
    protected byte[] engineUpdate(byte[] in, int inOfs, int inLen) {
        update(in, inOfs, inLen);
        return B0;
    }

    // see JCE spec
    @Override
    protected int engineUpdate(byte[] in, int inOfs, int inLen, byte[] out,
                               int outOfs) {
        update(in, inOfs, inLen);
        return 0;
    }

    // see JCE spec
    @Override
    protected byte[] engineDoFinal(byte[] in, int inOfs, int inLen)
            throws BadPaddingException, IllegalBlockSizeException {
        update(in, inOfs, inLen);
        return doFinal();
    }

    // see JCE spec
    @Override
    protected int engineDoFinal(byte[] in, int inOfs, int inLen, byte[] out, int outOfs)
            throws ShortBufferException, BadPaddingException, IllegalBlockSizeException {
        if (outputSize > out.length - outOfs) {
            throw new ShortBufferException
                    ("Need " + outputSize + " bytes for output");
        }
        update(in, inOfs, inLen);
        byte[] result = doFinal();
        int length = result.length;
        System.arraycopy(result, 0, out, outOfs, length);
        return length;
    }

    // see JCE spec
    @Override
    protected byte[] engineWrap(Key key) throws InvalidKeyException,
            IllegalBlockSizeException {
        byte[] encoded = key.getEncoded();
        if ((encoded == null) || (encoded.length == 0)) {
            throw new InvalidKeyException("Could not obtain encoded key");
        }
        if (encoded.length > buffer.length) {
            throw new InvalidKeyException("Key is too long for wrapping");
        }
        update(encoded, 0, encoded.length);
        try {
            return doFinal();
        } catch (BadPaddingException e) {
            // should not occur
            throw new InvalidKeyException("Wrapping failed", e);
        }
    }

    // see JCE spec
    @Override
    protected Key engineUnwrap(byte[] wrappedKey, String algorithm, int type)
            throws InvalidKeyException, NoSuchAlgorithmException {
        if (wrappedKey.length > buffer.length) {
            throw new InvalidKeyException("Key is too long for unwrapping");
        }

        boolean isTlsRsaPremasterSecret = "TlsRsaPremasterSecret".equals(algorithm);
        Exception failover = null;
        byte[] encoded = null;

        update(wrappedKey, 0, wrappedKey.length);
        try {
            encoded = doFinal();
        } catch (BadPaddingException e) {
            if (isTlsRsaPremasterSecret) {
                failover = e;
            } else {
                throw new InvalidKeyException("Unwrapping failed", e);
            }
        } catch (IllegalBlockSizeException e) {
            // should not occur, handled with length check above
            throw new InvalidKeyException("Unwrapping failed", e);
        }

        if (isTlsRsaPremasterSecret) {
            if (!(spec instanceof TlsRsaPremasterSecretParameterSpec)) {
                throw new IllegalStateException(
                        "No TlsRsaPremasterSecretParameterSpec specified");
            }

            // polish the TLS premaster secret
            encoded = KeyUtil.checkTlsPreMasterSecretKey(
                    ((TlsRsaPremasterSecretParameterSpec) spec).getClientVersion(),
                    ((TlsRsaPremasterSecretParameterSpec) spec).getServerVersion(),
                    random, encoded, (failover != null));
        }
        return KAEUtils.ConstructKeys.constructKey(encoded, algorithm, type);
    }

    // see JCE spec
    @Override
    protected int engineGetKeySize(Key key) throws InvalidKeyException {
        RSAKey newRSAKey = RSAKeyFactory.toRSAKey(key);
        return newRSAKey.getModulus().bitLength();
    }

    // reset the key holder
    private void resetKeyHolder() {
        if (rsaKeyHolder != null) {
            rsaKeyHolder.dispose(true);
            rsaKeyHolder = null;
        }
    }

    // create KAE rsa key
    protected static native long nativeCreateRSAPrivateCrtKey(byte[] n, byte[] e, byte[] d, byte[] p, byte[] q,
                                                              byte[] dmp1, byte[] dmq1, byte[] iqmp);

    // create KAE rsa public key
    protected static native long nativeCreateRSAPublicKey(byte[] n, byte[] e);

    // encrypt by private key for padding type (NOPADDING|PKCS1PADDING)
    protected static native int nativeRSAPrivateEncrypt(long keyAddress, int inLen, byte[] in, byte[] out,
                                                        int paddingType) throws BadPaddingException;

    // decrypt by private key for padding type (NOPADDING|PKCS1PADDING)
    protected static native int nativeRSAPrivateDecrypt(long keyAddress, int inLen, byte[] in, byte[] out,
                                                        int paddingType) throws BadPaddingException;

    // encrypt by public key for padding type (NOPADDING|PKCS1PADDING)
    protected static native int nativeRSAPublicEncrypt(long keyAddress, int inLen, byte[] in, byte[] out,
                                                       int paddingType) throws BadPaddingException;

    // decrypt by public key for padding type (NOPADDING|PKCS1PADDING)
    protected static native int nativeRSAPublicDecrypt(long keyAddress, int inLen, byte[] in, byte[] out,
                                                       int paddingType) throws BadPaddingException;

    // encrypt by public for padding type (OAEPPADDING)
    protected static native int nativeRSAEncryptOAEPPadding(long keyAddress, int inLen, byte[] in, byte[] out,
                                                            int paddingType, String oaepMdAlgo, String mgf1MdAlgo,
                                                            byte[] label) throws BadPaddingException;

    // decrypt by public for padding type (OAEPPADDING)
    protected static native int nativeRSADecryptOAEPPadding(long keyAddress, int inLen, byte[] in, byte[] out,
                                                            int paddingType, String oaepMdAlgo, String mgf1MdAlgo,
                                                            byte[] label) throws BadPaddingException;

    // free the key
    protected static native void nativeFreeKey(long keyAddress);

    /**
     * The rsa openssl key holder , use PhantomReference in case of native memory leaks
     */
    private static class KAERSAKeyHolder extends PhantomReference<KAERSACipher>
            implements Comparable<KAERSAKeyHolder> {
        private static ReferenceQueue<KAERSACipher> referenceQueue = new ReferenceQueue<>();
        private static Set<KAERSAKeyHolder> referenceList = new ConcurrentSkipListSet<>();
        private final long keyAddress;

        KAERSAKeyHolder(KAERSACipher rsaCipher, RSAKey rsaKey) throws InvalidKeyException {
            super(rsaCipher, referenceQueue);
            this.keyAddress = getKeyAddress(rsaKey);
            referenceList.add(this);
            drainRefQueueBounded();
        }

        private static void drainRefQueueBounded() {
            while (true) {
                KAERSAKeyHolder next = (KAERSAKeyHolder) referenceQueue.poll();
                if (next == null) {
                    break;
                }
                next.dispose(true);
            }
        }

        void dispose(boolean needFree) {
            referenceList.remove(this);
            try {
                if (needFree) {
                    nativeFreeKey(keyAddress);
                }
            } finally {
                this.clear();
            }
        }

        @Override
        public int compareTo(KAERSAKeyHolder other) {
            if (this.keyAddress == other.keyAddress) {
                return 0;
            } else {
                return (this.keyAddress < other.keyAddress) ? -1 : 1;
            }
        }

        private long getKeyAddress(RSAKey rsaKey) throws InvalidKeyException {
            long address;
            if (rsaKey instanceof RSAPrivateCrtKey) { // RSAPrivateCrtKeyImpl
                address = getKeyAddress((RSAPrivateCrtKey) rsaKey);
            } else if (rsaKey instanceof RSAPublicKey) { // RSAPublicKeyImpl
                address = getKeyAddress((RSAPublicKey) rsaKey);
            } else {
                throw new InvalidKeyException("Invalid RSAKey implement " + rsaKey.getClass());
            }
            return address;
        }

        private long getKeyAddress(RSAPrivateCrtKey key) throws InvalidKeyException {
            checkKey(key);
            long address;
            try {
                address = nativeCreateRSAPrivateCrtKey(
                        key.getModulus().toByteArray(),
                        key.getPublicExponent().toByteArray(),
                        key.getPrivateExponent().toByteArray(),
                        key.getPrimeP().toByteArray(),
                        key.getPrimeQ().toByteArray(),
                        key.getPrimeExponentP().toByteArray(),
                        key.getPrimeExponentQ().toByteArray(),
                        key.getCrtCoefficient().toByteArray());
                return address;
            } catch (Exception e) {
                throw new InvalidKeyException(e);
            }
        }

        private long getKeyAddress(RSAPublicKey key) throws InvalidKeyException {
            checkKey(key);
            long address;
            try {
                address = nativeCreateRSAPublicKey(
                        key.getModulus().toByteArray(),
                        key.getPublicExponent().toByteArray()
                );
                return address;
            } catch (Exception e) {
                throw new InvalidKeyException(e);
            }
        }

        private void checkKey(RSAPrivateCrtKey key) throws InvalidKeyException {
            if (key.getModulus() == null
                    || key.getPublicExponent() == null
                    || key.getPrivateExponent() == null
                    || key.getPrimeP() == null
                    || key.getPrimeQ() == null
                    || key.getPrimeExponentP() == null
                    || key.getPrimeExponentQ() == null
                    || key.getCrtCoefficient() == null) {
                throw new InvalidKeyException("Invalid RSA private key");
            }
        }

        private void checkKey(RSAPublicKey key) throws InvalidKeyException {
            if (key.getModulus() == null || key.getPublicExponent() == null) {
                throw new InvalidKeyException("Invalid RSA public key");
            }
        }
    }
}
