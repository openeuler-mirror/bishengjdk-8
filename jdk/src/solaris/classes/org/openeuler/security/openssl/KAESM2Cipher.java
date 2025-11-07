/*
 * Copyright (c) 2023, Huawei Technologies Co., Ltd. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Huawei designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Huawei in the LICENSE file that accompanied this code.
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
 * Please visit https://gitee.com/openeuler/bgmprovider if you need additional
 * information or have any questions.
 */

package org.openeuler.security.openssl;

import javax.crypto.*;
import java.io.ByteArrayOutputStream;
import java.lang.ref.PhantomReference;
import java.lang.ref.ReferenceQueue;
import java.security.*;
import java.security.interfaces.*;
import java.security.spec.AlgorithmParameterSpec;
import java.util.Arrays;
import java.util.Locale;
import java.util.Set;
import java.util.concurrent.ConcurrentSkipListSet;

import static org.openeuler.security.openssl.KAEUtils.asUnsignedByteArray;

public class KAESM2Cipher extends CipherSpi {
    // buffer for the data
    private KAEByteArrayOutputStream byteBuf = new KAEByteArrayOutputStream();

    private ECKey ecKey;
    private int cipherMode = -1;

    // sm2 key holder
    private KAESM2KeyHolder sm2KeyHolder;

    // see JCE spec
    @Override
    protected void engineSetMode(String mode) throws NoSuchAlgorithmException {
        String modeName = mode.toUpperCase(Locale.ROOT);

        if (!modeName.equals("NONE")) {
            throw new IllegalArgumentException("can't support mode " + mode);
        }
    }

    // see JCE spec
    @Override
    protected void engineSetPadding(String padding) throws NoSuchPaddingException {
        String paddingName = padding.toUpperCase(Locale.ROOT);

        if (!paddingName.equals("NOPADDING")) {
            throw new NoSuchPaddingException("padding not available with KAESM2Cipher");
        }
    }

    // see JCE spec
    @Override
    protected int engineGetBlockSize() {
        return 0;
    }

    // see JCE spec
    @Override
    protected int engineGetOutputSize(int inputLen) {
        throw new UnsupportedOperationException("engineGetOutputSize");
    }

    // see JCE spec
    @Override
    protected byte[] engineGetIV() {
        return null;
    }

    // see JCE spec
    @Override
    protected AlgorithmParameters engineGetParameters() {
        return null;
    }

    // see JCE spec
    @Override
    protected byte[] engineWrap(Key key)
            throws IllegalBlockSizeException, InvalidKeyException {
        if (key == null) {
            throw new InvalidKeyException("Key cannot be null");
        }
        byte[] encoded = key.getEncoded();
        if ((encoded == null) || (encoded.length == 0)) {
            throw new InvalidKeyException("Cannot get an encoding of " +
                    "the key to be wrapped");
        }
        try {
            return engineDoFinal(encoded, 0, encoded.length);
        } catch (BadPaddingException e) {
            throw new InvalidKeyException("Wrapping failed", e);
        }
    }

    // see JCE spec
    @Override
    protected Key engineUnwrap(byte[] wrappedKey, String wrappedKeyAlgorithm, int wrappedKeyType)
            throws InvalidKeyException, NoSuchAlgorithmException {
        if (wrappedKey == null || wrappedKey.length == 0) {
            throw new InvalidKeyException("The wrappedKey cannot be null or empty");
        }
        byte[] unWrappedKey;
        try {
            unWrappedKey = engineDoFinal(wrappedKey, 0, wrappedKey.length);
        } catch (IllegalBlockSizeException | BadPaddingException e) {
            throw new InvalidKeyException("Unwrapping failed", e);
        }
        return KAEUtils.ConstructKeys.constructKey(unWrappedKey, wrappedKeyAlgorithm, wrappedKeyType);
    }

    // see JCE spec
    @Override
    protected void engineInit(int opmode, Key key, SecureRandom random) throws InvalidKeyException {
        try {
            engineInit(opmode, key, (AlgorithmParameterSpec) null, random);
        } catch (InvalidAlgorithmParameterException e) {
            throw new IllegalArgumentException("cannot handle supplied parameter spec: " + e.getMessage());
        }
    }

    // see JCE spec
    @Override
    protected void engineInit(int opmode, Key key, AlgorithmParameterSpec params, SecureRandom random) throws InvalidKeyException, InvalidAlgorithmParameterException {
        if (opmode == Cipher.ENCRYPT_MODE || opmode == Cipher.WRAP_MODE) {
            if (key instanceof KAEECPublicKeyImpl) {
                this.ecKey = (KAEECPublicKeyImpl) key;
            } else if (key instanceof ECPublicKey) {
                this.ecKey = (ECPublicKey) key;
            } else {
                throw new InvalidKeyException("must use public EC key for encryption");
            }
        } else if (opmode == Cipher.DECRYPT_MODE || opmode == Cipher.UNWRAP_MODE) {
            if (key instanceof KAEECPrivateKeyImpl) {
                this.ecKey = (KAEECPrivateKeyImpl) key;
            } else if (key instanceof ECPrivateKey) {
                this.ecKey = (ECPrivateKey) key;
            } else {
                throw new InvalidKeyException("must use private EC key for decryption");
            }
        } else {
            throw new InvalidParameterException("wrong cipher mode, must be ENCRYPT_MODE or WRAP_MODE or DECRYPT_MODE or UNWRAP_MODE");
        }

        try {
            sm2KeyHolder = new KAESM2KeyHolder(this, ecKey);
        } catch (InvalidKeyException e) {
            throw new RuntimeException(e);
        }
        this.cipherMode = opmode;
        this.byteBuf.reset();
    }

    // see JCE spec
    @Override
    protected void engineInit(int opmode, Key key, AlgorithmParameters params, SecureRandom random) throws InvalidKeyException, InvalidAlgorithmParameterException {
        AlgorithmParameterSpec paramSpec = null;
        if (params != null) {
            throw new InvalidAlgorithmParameterException("cannot recognise parameters: " + params.getClass().getName());
        }
        engineInit(opmode, key, paramSpec, random);
    }

    // see JCE spec
    @Override
    protected byte[] engineUpdate(byte[] input, int inputOffset, int inputLen) {
        byteBuf.write(input, inputOffset, inputLen);
        return null;
    }

    // see JCE spec
    @Override
    protected int engineUpdate(byte[] input, int inputOffset, int inputLen, byte[] output, int outputOffset) throws ShortBufferException {
        engineUpdate(input, inputOffset, inputLen);
        return 0;
    }

    // see JCE spec
    @Override
    protected byte[] engineDoFinal(byte[] input, int inputOffset, int inputLen)
            throws IllegalBlockSizeException, BadPaddingException {
        if (inputLen != 0) {
            byteBuf.write(input, inputOffset, inputLen);
        }
        if(byteBuf.size() == 0){
            throw new IllegalBlockSizeException("input buffer too short");
        }
        if (sm2KeyHolder == null) {
            try {
                sm2KeyHolder = new KAESM2KeyHolder(this, ecKey);
            } catch (InvalidKeyException e) {
                throw new RuntimeException(e);
            }
        }

        long keyAddress = sm2KeyHolder.keyAddress;
        byte[] out;
        try {
            if (cipherMode == Cipher.ENCRYPT_MODE || cipherMode == Cipher.WRAP_MODE) {
                try {
                    out = nativeSM2Encrypt(keyAddress, byteBuf.toByteArray(), byteBuf.size());
                } catch (RuntimeException e) {
                    throw new RuntimeException("KAESM2Cipher native encryption failed: " , e);
                }
            } else if (cipherMode == Cipher.DECRYPT_MODE || cipherMode == Cipher.UNWRAP_MODE) {
                try {
                    out = nativeSM2Decrypt(keyAddress, byteBuf.toByteArray(), byteBuf.size());
                } catch (RuntimeException e) {
                    throw new RuntimeException("KAESM2Cipher native decryption failed: " , e);
                }
            } else {
                throw new IllegalStateException("cipher not initialised");
            }
        } finally {
            byteBuf.reset();
            resetKeyHolder();
        }
        return out;
    }

    // see JCE spec
    @Override
    protected int engineDoFinal(byte[] input, int inputOffset, int inputLen, byte[] output, int outputOffset)
            throws ShortBufferException, IllegalBlockSizeException, BadPaddingException {
        byte[] buffer = engineDoFinal(input, inputOffset, inputLen);
        System.arraycopy(buffer, 0, output, outputOffset, buffer.length);
        return buffer.length;
    }

    /**
     * The sm2 openssl key holder , use PhantomReference in case of native memory leaks
     */
    private static class KAESM2KeyHolder extends PhantomReference<KAESM2Cipher>
            implements Comparable<KAESM2KeyHolder> {
        private static ReferenceQueue<KAESM2Cipher> referenceQueue = new ReferenceQueue<>();
        private static Set<KAESM2KeyHolder> referenceList = new ConcurrentSkipListSet<>();
        private final long keyAddress;

        private static boolean disableKaeDispose = Boolean.getBoolean("kae.disableKaeDispose");

        KAESM2KeyHolder(KAESM2Cipher sm2Cipher, ECKey sm2Key) throws InvalidKeyException {
            super(sm2Cipher, referenceQueue);
            this.keyAddress = getKeyAddress(sm2Key);
            if (!disableKaeDispose) {
                referenceList.add(this);
                drainRefQueueBounded();
            }
        }

        private static void drainRefQueueBounded() {
            while (true) {
                KAESM2KeyHolder next = (KAESM2KeyHolder) referenceQueue.poll();
                if (next == null) {
                    break;
                }
                next.dispose(true);
            }
        }

        void dispose(boolean needFree) {
            if (!disableKaeDispose) {
                referenceList.remove(this);
                try {
                    if (needFree) {
                        nativeFreeKey(keyAddress);
                    }
                } finally {
                    this.clear();
                }
            } else {
                nativeFreeKey(keyAddress);
            }
        }

        @Override
        public int compareTo(KAESM2KeyHolder other) {
            if (this.keyAddress == other.keyAddress) {
                return 0;
            } else {
                return (this.keyAddress < other.keyAddress) ? -1 : 1;
            }
        }

        private long getKeyAddress(ECKey sm2Key) throws InvalidKeyException {
            long address;
            if (sm2Key instanceof ECPrivateKey) { // ECPrivateKeyImpl
                address = getKeyAddress((ECPrivateKey) sm2Key);
            } else if (sm2Key instanceof ECPublicKey) { // ECPublicKeyImpl
                address = getKeyAddress((ECPublicKey) sm2Key);
            } else {
                throw new InvalidKeyException("Invalid SM2Key implement " + sm2Key.getClass());
            }
            return address;
        }

        private long getKeyAddress(ECPrivateKey key) throws InvalidKeyException {
            checkKey(key);
            long address;
            int curveLen = (key.getParams().getCurve().getField().getFieldSize() + 7) / 8;
            try {
                address = nativeCreateSM2PrivateKey(asUnsignedByteArray(curveLen, key.getS()), false);
                return address;
            } catch (RuntimeException e) {
                throw new InvalidKeyException(e);
            }
        }

        private long getKeyAddress(ECPublicKey key) throws InvalidKeyException {
            checkKey(key);
            long address;
            int curveLen = (key.getParams().getCurve().getField().getFieldSize() + 7) / 8;
            try {
                address = nativeCreateSM2PublicKey(
                        asUnsignedByteArray(curveLen, key.getW().getAffineX()),
                        asUnsignedByteArray(curveLen, key.getW().getAffineY())
                );
                return address;
            } catch (RuntimeException e) {
                throw new InvalidKeyException(e);
            }
        }

        private void checkKey(ECPrivateKey key) throws InvalidKeyException {
            if (key.getS() == null) {
                throw new InvalidKeyException("Invalid SM2 private key");
            }
        }

        private void checkKey(ECPublicKey key) throws InvalidKeyException {
            if (key.getW() == null || key.getW().getAffineX() == null || key.getW().getAffineY() == null) {
                throw new InvalidKeyException("Invalid SM2 public key");
            }
        }
    }

    // reset the key holder
    private void resetKeyHolder() {
        if (sm2KeyHolder != null) {
            sm2KeyHolder.dispose(true);
            sm2KeyHolder = null;
        }
    }

    // create KAE sm2 private key
    protected static native long nativeCreateSM2PublicKey(byte[] x, byte[] y);

    // create KAE sm2 public key
    protected static native long nativeCreateSM2PrivateKey(byte[] key, boolean sign);

    // free the key
    protected static native void nativeFreeKey(long keyAddress);

    // Encrypt message using sm2 algorithm
    protected static native byte[] nativeSM2Encrypt(long keyAddress, byte[] input, int inputLen);

    // Decrypt message using sm2 algorithm
    protected static native byte[] nativeSM2Decrypt(long keyAddress, byte[] input, int inputLen);

    private static class KAEByteArrayOutputStream extends ByteArrayOutputStream {
        @Override
        public synchronized void reset() {
            // Clear data.
            Arrays.fill(buf, (byte) 0);
            super.reset();
        }
    }
}
