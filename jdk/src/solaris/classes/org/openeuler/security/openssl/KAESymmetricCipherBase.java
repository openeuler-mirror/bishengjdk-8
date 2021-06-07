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

import sun.security.jca.JCAUtil;

import java.lang.ref.PhantomReference;
import java.lang.ref.ReferenceQueue;
import java.nio.ByteBuffer;
import java.security.*;
import java.security.spec.*;
import java.util.Arrays;
import java.util.Set;
import java.util.concurrent.ConcurrentSkipListSet;

import javax.crypto.*;
import javax.crypto.spec.GCMParameterSpec;
import javax.crypto.spec.IvParameterSpec;
import javax.crypto.spec.SecretKeySpec;

/*
 * Cipher wrapper class utilizing openssl APIs.
 */
abstract class KAESymmetricCipherBase extends CipherSpi {
    enum Padding {
        NOPADDING,
        PKCS5PADDING
    }

    enum Mode {
        ECB,
        CBC,
        CTR,
        OFB,
        GCM
    }

    protected final String keyAlgo;
    protected final int blockSize = 16;
    protected Mode mode;
    protected Padding padding;
    protected int fixedKeySize;

    private CipherContextRef pCtx = null;
    private byte[] keyValue;
    protected byte[] iv;
    private boolean initialized = false;
    private boolean encrypt = false;
    private int bytesBuffered = 0;

    private boolean calledUpdate;
    private String cipherName;

    // for gcm
    private final int defaultGcmTagLen = blockSize;
    private final int defaultGcmIvLen = 12;
    private int tagLengthInBytes;
    private byte[] lastEncKey = null;
    private byte[] lastEncIv = null;
    private byte[] aad;

    private static PublicKey constructPublicKey(byte[] encodedKey, String encodedKeyAlgorithm)
            throws NoSuchAlgorithmException, InvalidKeyException {
        PublicKey key;
        try {
            KeyFactory keyFactory = KeyFactory.getInstance(encodedKeyAlgorithm);
            X509EncodedKeySpec keySpec = new X509EncodedKeySpec(encodedKey);
            key = keyFactory.generatePublic(keySpec);
        } catch (NoSuchAlgorithmException e) {
            throw new NoSuchAlgorithmException("No provider found for " + encodedKeyAlgorithm + " KeyFactory");
        } catch (InvalidKeySpecException e) {
            throw new InvalidKeyException("Cannot construct public key", e);
        }
        return key;
    }

    private static PrivateKey constructPrivateKey(byte[] encodedKey,
            String encodedKeyAlgorithm) throws InvalidKeyException, NoSuchAlgorithmException {
        PrivateKey key = null;
        try {
            KeyFactory keyFactory = KeyFactory.getInstance(encodedKeyAlgorithm);
            PKCS8EncodedKeySpec keySpec = new PKCS8EncodedKeySpec(encodedKey);
            key = keyFactory.generatePrivate(keySpec);
        } catch (NoSuchAlgorithmException e) {
            throw new NoSuchAlgorithmException("No provider found for " + encodedKeyAlgorithm + " KeyFactory");
        } catch (InvalidKeySpecException e) {
            throw new InvalidKeyException("Cannot construct private key", e);
        }
        return key;
    }

    private static SecretKey constructSecretKey(byte[] encodedKey, String encodedKeyAlgorithm) {
        return new SecretKeySpec(encodedKey, encodedKeyAlgorithm);
    }

    static final Key constructKey(int keyType, byte[] encodedKey,
            String encodedKeyAlgorithm) throws NoSuchAlgorithmException, InvalidKeyException {
        Key res = null;
        switch (keyType) {
            case Cipher.SECRET_KEY:
                res = constructSecretKey(encodedKey, encodedKeyAlgorithm);
                break;
            case Cipher.PRIVATE_KEY:
                res = constructPrivateKey(encodedKey, encodedKeyAlgorithm);
                break;
            case Cipher.PUBLIC_KEY:
                res = constructPublicKey(encodedKey, encodedKeyAlgorithm);
                break;
            default:
                throw new InvalidKeyException("Unknown keytype " + keyType);
        }
        return res;
    }

    KAESymmetricCipherBase(Mode mode, Padding padding, int fixedKeySize, String keyAlgo) {
        this.mode = mode;
        this.padding = padding;
        this.fixedKeySize = fixedKeySize;
        this.keyAlgo = keyAlgo;
    }

    private static class CipherContextRef extends PhantomReference<KAESymmetricCipherBase>
            implements Comparable<CipherContextRef> {
        private static ReferenceQueue<KAESymmetricCipherBase> refQueue = new ReferenceQueue<>();
        private static Set<CipherContextRef> refList = new ConcurrentSkipListSet<>();
        private static boolean disableKaeDispose = Boolean.getBoolean("kae.disableKaeDispose");

        final long ctxAddress;

        private static void drainRefQueueBounded() {
            while (true) {
                CipherContextRef next = (CipherContextRef) refQueue.poll();
                if (next == null) {
                    break;
                }
                next.dispose(true);
            }
        }

        CipherContextRef(KAESymmetricCipherBase kaeCipher, long ctxAddress) {
            super(kaeCipher, refQueue);
            this.ctxAddress = ctxAddress;
            if (!disableKaeDispose) {
                refList.add(this);
                drainRefQueueBounded();
            }
        }

        @Override
        public int compareTo(CipherContextRef o) {
            if (this.ctxAddress == o.ctxAddress) {
                return 0;
            } else {
                return (this.ctxAddress < o.ctxAddress) ? -1 : 1;
            }
        }

        void dispose(boolean needFree) {
            if (!disableKaeDispose) {
                refList.remove(this);
                try {
                    if (needFree) {
                        nativeFree(ctxAddress);
                    }
                } finally {
                    this.clear();
                }
            } else {
                nativeFree(ctxAddress);
            }
        }
    }

    @Override
    protected int engineGetBlockSize() {
        return blockSize;
    }

    @Override
    protected int engineGetOutputSize(int inputLen) {
        return getOutputSizeByOperation(inputLen, true);
    }

    @Override
    protected byte[] engineGetIV() {
        return iv == null ? null : iv.clone();
    }

    @Override
    protected AlgorithmParameters engineGetParameters() {
        if (iv == null) {
            return null;
        }
        AlgorithmParameterSpec spec;
        AlgorithmParameters params;
        String algName = keyAlgo;
        if (mode == Mode.GCM) {
            algName = "GCM";
            spec = new GCMParameterSpec(tagLengthInBytes * 8, iv.clone());
        } else {
            spec = new IvParameterSpec(iv.clone());
        }
        try {
            params = AlgorithmParameters.getInstance(algName);
            params.init(spec);
            return params;
        } catch (GeneralSecurityException e) {
            throw new RuntimeException("Could not encode parameters", e);
        }
    }

    @Override
    protected void engineInit(int opmode, Key key, SecureRandom random) throws InvalidKeyException {
        try {
            engineInit(opmode, key, (AlgorithmParameterSpec) null, random);
        } catch (InvalidAlgorithmParameterException e) {
            throw new InvalidKeyException("init() failed", e);
        }
    }

    @Override
    protected void engineInit(int opmode, Key key, AlgorithmParameters params,
                    SecureRandom random) throws InvalidKeyException, InvalidAlgorithmParameterException {
        AlgorithmParameterSpec spec = null;
        String paramType = null;
        if (params != null) {
            try {
                if (mode == Mode.GCM) {
                    spec = params.getParameterSpec(GCMParameterSpec.class);
                    paramType = "GCM";
                } else {
                    spec = params.getParameterSpec(IvParameterSpec.class);
                    paramType = "IV";
                }
            } catch (InvalidParameterSpecException e) {
                throw new InvalidAlgorithmParameterException("Could not decode " + paramType, e);
            }
        }
        engineInit(opmode, key, spec, random);
    }

    @Override
    protected void engineInit(int opmode, Key key, AlgorithmParameterSpec params,
                    SecureRandom random) throws InvalidKeyException, InvalidAlgorithmParameterException {
        checkKey(key);
        boolean doEncrypt = (opmode == Cipher.ENCRYPT_MODE || opmode == Cipher.WRAP_MODE);

        byte[] ivBytes = null;
        int tagLen = -1;
        if (params != null) {
            if (mode == Mode.GCM) {
                if (params instanceof GCMParameterSpec) {
                    tagLen = ((GCMParameterSpec)params).getTLen();
                    checkTagLen(tagLen);
                    tagLen = tagLen >> 3;
                    ivBytes = ((GCMParameterSpec)params).getIV();
                } else {
                    throw new InvalidAlgorithmParameterException("Unsupported parameter: " + params);
                }
            } else {
                if (params instanceof IvParameterSpec) {
                    ivBytes = ((IvParameterSpec) params).getIV();
                    checkIvBytes(ivBytes);
                } else {
                    throw new InvalidKeyException("IvParameterSpec required. Received: " + params.getClass().getName());
                }
            }
        }
        if (mode == Mode.ECB) {
            if (params != null) {
                throw new InvalidAlgorithmParameterException("No Parameters for ECB mode");
            }
        } else if (ivBytes == null) {
            if (doEncrypt) {
                if (mode == Mode.GCM) {
                    ivBytes = new byte[defaultGcmIvLen];
                } else {
                    ivBytes = new byte[blockSize];
                }
                if (random == null) {
                    random = JCAUtil.getSecureRandom();
                }
                random.nextBytes(ivBytes);
            } else {
                throw new InvalidAlgorithmParameterException("Parameters required for decryption");
            }
        } else if (keyAlgo.equalsIgnoreCase("SM4") && ivBytes.length < blockSize) {
            byte[] temp = new byte[blockSize];
            System.arraycopy(ivBytes, 0, temp, 0, ivBytes.length);
            ivBytes = temp;
        }
        implInit(doEncrypt, key.getEncoded(), ivBytes, tagLen);
    }

    private void checkTagLen(int tagLen) throws InvalidAlgorithmParameterException {
        if ((tagLen < 96) || (tagLen > 128) || ((tagLen & 0x07) != 0)) {
            throw new InvalidAlgorithmParameterException
                ("Unsupported TLen value; must be one of {128, 120, 112, 104, 96}");
        }
    }

    protected abstract void checkIvBytes(byte[] ivBytes) throws InvalidAlgorithmParameterException;

    protected abstract String getCipherName(int keyLength, Mode mode);

    private void implInit(boolean encrypt, byte[] keyVal, byte[] ivVal, int tagLen)
            throws InvalidAlgorithmParameterException {
        reset(true);
        this.encrypt = encrypt;
        this.keyValue = keyVal;
        this.iv = ivVal;
        this.cipherName = getCipherName(keyValue.length * 8, mode);

        if (mode == Mode.GCM) {
            if (tagLen == -1) {
                tagLen = defaultGcmTagLen;
            }
            this.tagLengthInBytes = tagLen;
            if (encrypt) {
                // Check key+iv for encryption in GCM mode.
                boolean requireReinit = Arrays.equals(ivVal, lastEncIv) && MessageDigest.isEqual(keyVal, lastEncKey);
                if (requireReinit) {
                    throw new InvalidAlgorithmParameterException("Cannot reuse iv for GCM encryption");
                }
                lastEncIv = ivVal;
                lastEncKey = keyVal;
            }
        }

        // OpenSSL only supports PKCS5 Padding.
        long pCtxVal;
        try {
            pCtxVal = nativeInit(cipherName, encrypt, keyValue, iv, padding == Padding.PKCS5PADDING);
        } catch (RuntimeException e) {
            throw new ProviderException("Invoke nativeInit failed for " + cipherName, e);
        }

        initialized = (pCtxVal != 0L);
        if (initialized) {
            pCtx = new CipherContextRef(this, pCtxVal);
        } else {
            throw new NullPointerException("pCtxVal == 0");
        }
        calledUpdate = false;
    }

    protected abstract void checkKey(Key key) throws InvalidKeyException;

    @Override
    protected byte[] engineUpdate(byte[] input, int inputOffset, int inputLen) {
        byte[] out = new byte[getOutputSizeByOperation(inputLen, false)];
        int outLen = implUpdate(input, inputOffset, inputLen, out, 0);
        if (outLen == 0) {
            return new byte[0];
        } else if (out.length != outLen) {
            out = Arrays.copyOf(out, outLen);
        }
        return out;
    }

    @Override
    protected int engineUpdate(byte[] input, int inputOffset, int inputLen, byte[] output,
                    int outputOffset) throws ShortBufferException {
        int min = getOutputSizeByOperation(inputLen, false);
        if (output == null || output.length - outputOffset < min) {
            throw new ShortBufferException("min " + min + "-byte buffer needed");
        }
        return implUpdate(input, inputOffset, inputLen, output, outputOffset);
    }

    private int implUpdate(byte[] input, int inputOffset, int inputLen, byte[] output, int outputOffset) {
        ensureInitialized();
        if (inputLen <= 0) {
            return 0;
        }
        int outLen;
        try {
            outLen = nativeUpdate(pCtx.ctxAddress, input, inputOffset, inputLen, output, outputOffset,
                    mode == Mode.GCM, aad);
            aad = null;
        } catch (ArrayIndexOutOfBoundsException e) {
            reset(true);
            throw new ProviderException("Invoke nativeUpdate failed for " + cipherName, e);
        }
        bytesBuffered += (inputLen - outLen);

        calledUpdate = true;
        return outLen;
    }

    protected int getOutputSizeByOperation(int inLen, boolean isDoFinal) {
        int ret;

        if (inLen <= 0) {
            inLen = 0;
        }
        if (padding == Padding.NOPADDING) {
            ret = inLen + bytesBuffered;
        } else {
            int len = inLen + bytesBuffered;

            // The amount of data written may be anything from zero bytes to (inl + cipher_block_size - 1) for encrypt.
            // Refer to {@link https://www.openssl.org/docs/man1.1.0/man3/EVP_CipherUpdate.html} for details.
            len += (len % blockSize != 0 || encrypt) ? blockSize : 0;
            ret = len - (len % blockSize);
        }
        if (mode == Mode.GCM && isDoFinal) {
            if (encrypt) {
                ret = ret + tagLengthInBytes;
            } else {
                ret = Math.max(0, ret - tagLengthInBytes);
            }
        }
        return ret;
    }

    @Override
    protected byte[] engineDoFinal(byte[] input, int inputOffset,
                    int inputLen) throws IllegalBlockSizeException, BadPaddingException {
        byte[] out = new byte[getOutputSizeByOperation(inputLen, true)];
        try {
            int outLen = engineDoFinal(input, inputOffset, inputLen, out, 0);
            if (out.length != outLen) {
                out = Arrays.copyOf(out, outLen);
            }
            return out;
        } catch (ShortBufferException e) {
            throw new ProviderException(e);
        }
    }

    @Override
    protected int engineDoFinal(byte[] input, int inputOffset, int inputLen, byte[] output,
                    int outputOffset) throws ShortBufferException, IllegalBlockSizeException, BadPaddingException {
        int outLen = 0;
        int min = getOutputSizeByOperation(inputLen, true);
        if (output == null || output.length - outputOffset < min) {
            throw new ShortBufferException("min " + min + "-byte buffer needed");
        }

        int updateLen = inputLen;
        if (mode == Mode.GCM && !encrypt) {
            // Remove tagLengthInBytes suffix in GCM decrypt.
            updateLen = inputLen - tagLengthInBytes;
        }
        outLen = implUpdate(input, inputOffset, updateLen, output, outputOffset);
        outputOffset += outLen;

        byte[] gcmTag = null;
        if (mode == Mode.GCM && !encrypt) {
            if (inputLen - outLen != tagLengthInBytes) {
                throw new AEADBadTagException("Tag mismatch!");
            }
            // The last tagLengthInBytees in the input arg gcmTag.
            gcmTag = Arrays.copyOfRange(input, inputOffset + inputLen - tagLengthInBytes, inputOffset + inputLen);
        }

        outLen += implDoFinal(output, outputOffset, gcmTag);
        return outLen;
    }

    @Override
    protected byte[] engineWrap(Key key) throws IllegalBlockSizeException, InvalidKeyException {
        byte[] res = null;
        try {
            byte[] encodedKey = key.getEncoded();
            if (encodedKey == null || encodedKey.length == 0) {
                throw new InvalidKeyException("Cannot get an encoding of the key to be wrapped");
            }
            res = engineDoFinal(encodedKey, 0, encodedKey.length);
        } catch (BadPaddingException e) {
            // Should never happen
        }
        return res;
    }

    @Override
    protected Key engineUnwrap(byte[] wrappedKey, String wrappedKeyAlgorithm,
                    int wrappedKeyType) throws InvalidKeyException, NoSuchAlgorithmException {
        byte[] encodedKey;
        try {
            encodedKey = engineDoFinal(wrappedKey, 0, wrappedKey.length);
        } catch (IllegalBlockSizeException | BadPaddingException e) {
            throw (InvalidKeyException) (new InvalidKeyException()).initCause(e);
        }
        return constructKey(wrappedKeyType, encodedKey, wrappedKeyAlgorithm);
    }

    @Override
    protected void engineUpdateAAD(ByteBuffer byteBuffer) {
        if (aad == null) {
            aad = new byte[byteBuffer.remaining()];
            byteBuffer.get(aad);
        } else {
            int newSize = aad.length + byteBuffer.remaining();
            byte[] newaad = new byte[newSize];
            System.arraycopy(aad, 0, newaad, 0, aad.length);
            byteBuffer.get(newaad, aad.length, byteBuffer.remaining());
            aad = newaad;
        }
    }

    @Override
    protected void engineUpdateAAD(byte[] input, int inputOffset, int inputLen) {
        if (aad == null) {
            aad = Arrays.copyOfRange(input, inputOffset, inputOffset + inputLen);
        } else {
            int newSize = aad.length + inputLen;
            byte[] newaad = new byte[newSize];
            System.arraycopy(aad, 0, newaad, 0, aad.length);
            System.arraycopy(input, inputOffset, newaad, aad.length, inputLen);
            aad = newaad;
        }
    }

    private int implDoFinal(byte[] out, int outputOffset, byte[] gcmTag)
            throws BadPaddingException, IllegalBlockSizeException {
        if (!encrypt && !calledUpdate) {
            return 0;
        }
        ensureInitialized();

        int outLen;
        try {
            if (mode == Mode.GCM) {
                outLen = nativeFinalGcm(pCtx.ctxAddress, out, outputOffset, mode == Mode.GCM, tagLengthInBytes,
                        gcmTag, encrypt);
            } else {
                outLen = nativeFinal(pCtx.ctxAddress, out, outputOffset);
            }
        } catch (ArrayIndexOutOfBoundsException | BadPaddingException e) {
            if (e instanceof AEADBadTagException) {
                throw e; // AEADBadTagException is expected for some tests
            } else if (e instanceof BadPaddingException) {
                if (padding == Padding.NOPADDING || e.getMessage().contains("wrong final block length")) {
                    throw new IllegalBlockSizeException("Input length not multiple of " + blockSize + " bytes");
                } else {
                    throw e;
                }
            } else {
                throw new ProviderException("Invoke nativeFinal failed for " + cipherName, e);
            }
        } finally {
            reset(true);
        }

        return outLen;
    }

    protected void reset(boolean doCancel) {
        initialized = false;
        bytesBuffered = 0;
        calledUpdate = false;

        // for gcm
        aad = null;

        if (pCtx != null) {
            pCtx.dispose(doCancel);
            pCtx = null;
        }
    }

    protected static native long nativeInit(String cipherType, boolean encrypt, byte[] key, byte[] iv, boolean padding)
            throws RuntimeException;

    protected static native int nativeUpdate(long pContext, byte[] in, int inOfs, int inLen, byte[] out,
            int outOfs, boolean gcm, byte[] aad) throws ArrayIndexOutOfBoundsException;

    protected static native int nativeFinal(long pContext, byte[] out,
                    int outOfs) throws ArrayIndexOutOfBoundsException, BadPaddingException;

    protected static native void nativeFree(long pContext);

    protected static native int nativeFinalGcm(long pContext, byte[] out, int outOfs, boolean gcm,
            int tagLength, byte[] gcmTag, boolean encrypt) throws ArrayIndexOutOfBoundsException, BadPaddingException;

    protected void ensureInitialized() {
        if (!initialized) {
            reset(true);
            long pCtxVal = nativeInit(cipherName, encrypt, keyValue, iv, padding == Padding.PKCS5PADDING);
            initialized = (pCtxVal != 0L);
            if (initialized) {
                pCtx = new CipherContextRef(this, pCtxVal);
            } else {
                throw new RuntimeException("Cannot initialize Cipher");
            }
        }
    }
}

