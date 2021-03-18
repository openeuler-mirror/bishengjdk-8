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
import java.security.*;
import java.security.spec.*;
import java.util.Arrays;
import java.util.Locale;
import java.util.Set;
import java.util.concurrent.ConcurrentSkipListSet;

import javax.crypto.*;
import javax.crypto.spec.IvParameterSpec;
import javax.crypto.spec.SecretKeySpec;

/*
 * Cipher wrapper class utilizing openssl APIs. This class currently supports
 * - AES/ECB/NOPADDING
 * - AES/ECB/PKCS5PADDING
 * - AES/CBC/NOPADDING
 * - AES/CBC/PKCS5PADDING
 * - AES/CTR/NOPADDING
 */
abstract class KAEAESCipher extends CipherSpi {

    public static class Aes extends KAEAESCipher {
        public Aes(Mode mode, Padding padding) {
            super(mode, padding, -1);
        }

        public static class Cbc extends Aes {
            public Cbc(Padding padding) {
                super(Mode.CBC, padding);
            }
            public static class NoPadding extends Cbc {
                public NoPadding() {
                    super(Padding.NOPADDING);
                }
            }
            public static class PKCS5Padding extends Cbc {
                public PKCS5Padding() {
                    super(Padding.PKCS5PADDING);
                }
            }
        }
        public static class Ecb extends Aes {
            public Ecb(Padding padding) {
                super(Mode.ECB, padding);
            }
            public static class NoPadding extends Ecb {
                public NoPadding() {
                    super(Padding.NOPADDING);
                }
            }
            public static class PKCS5Padding extends Ecb {
                public PKCS5Padding() {
                    super(Padding.PKCS5PADDING);
                }
            }
        }

        public static class Ctr extends Aes {
            public Ctr(Padding padding) {
                super(Mode.CTR, padding);
            }
            public static class NoPadding extends Ctr {
                public NoPadding() {
                    super(Padding.NOPADDING);
                }
            }
        }
    }

    public static class Aes_128 extends KAEAESCipher {
        public Aes_128(Mode mode, Padding padding) {
            super(mode, padding, 16);
        }
        public static class Cbc extends Aes_128 {
            public Cbc(Padding padding) {
                super(Mode.CBC, padding);
            }
            public static class NoPadding extends Cbc {
                public NoPadding() {
                    super(Padding.NOPADDING);
                }
            }
            public static class PKCS5Padding extends Cbc {
                public PKCS5Padding() {
                    super(Padding.PKCS5PADDING);
                }
            }
        }
        public static class Ecb extends Aes_128 {
            public Ecb(Padding padding) {
                super(Mode.ECB, padding);
            }
            public static class NoPadding extends Ecb {
                public NoPadding() {
                    super(Padding.NOPADDING);
                }
            }
            public static class PKCS5Padding extends Ecb {
                public PKCS5Padding() {
                    super(Padding.PKCS5PADDING);
                }
            }
        }

        public static class Ctr extends Aes_128 {
            public Ctr(Padding padding) {
                super(Mode.CTR, padding);
            }
            public static class NoPadding extends Ctr {
                public NoPadding() {
                    super(Padding.NOPADDING);
                }
            }
        }
    }

    public static class Aes_192 extends KAEAESCipher {
        public Aes_192(Mode mode, Padding padding) {
            super(mode, padding, 24);
        }
        public static class Cbc extends Aes_192 {
            public Cbc(Padding padding) {
                super(Mode.CBC, padding);
            }
            public static class NoPadding extends Cbc {
                public NoPadding() {
                    super(Padding.NOPADDING);
                }
            }
            public static class PKCS5Padding extends Cbc {
                public PKCS5Padding() {
                    super(Padding.PKCS5PADDING);
                }
            }
        }
        public static class Ecb extends Aes_192 {
            public Ecb(Padding padding) {
                super(Mode.ECB, padding);
            }
            public static class NoPadding extends Ecb {
                public NoPadding() {
                    super(Padding.NOPADDING);
                }
            }
            public static class PKCS5Padding extends Ecb {
                public PKCS5Padding() {
                    super(Padding.PKCS5PADDING);
                }
            }
        }

        public static class Ctr extends Aes_192 {
            public Ctr(Padding padding) {
                super(Mode.CTR, padding);
            }
            public static class NoPadding extends Ctr {
                public NoPadding() {
                    super(Padding.NOPADDING);
                }
            }
        }
    }

    public static class Aes_256 extends KAEAESCipher {
        public Aes_256(Mode mode, Padding padding) {
            super(mode, padding, 32);
        }
        public static class Cbc extends Aes_256 {
            public Cbc(Padding padding) {
                super(Mode.CBC, padding);
            }
            public static class NoPadding extends Cbc {
                public NoPadding() {
                    super(Padding.NOPADDING);
                }
            }
            public static class PKCS5Padding extends Cbc {
                public PKCS5Padding() {
                    super(Padding.PKCS5PADDING);
                }
            }
        }
        public static class Ecb extends Aes_256 {
            public Ecb(Padding padding) {
                super(Mode.ECB, padding);
            }
            public static class NoPadding extends Ecb {
                public NoPadding() {
                    super(Padding.NOPADDING);
                }
            }
            public static class PKCS5Padding extends Ecb {
                public PKCS5Padding() {
                    super(Padding.PKCS5PADDING);
                }
            }
        }

        public static class Ctr extends Aes_256 {
            public Ctr(Padding padding) {
                super(Mode.CTR, padding);
            }
            public static class NoPadding extends Ctr {
                public NoPadding() {
                    super(Padding.NOPADDING);
                }
            }
        }
    }

    enum Padding {
        NOPADDING,
        PKCS5PADDING
    }

    enum Mode {
        ECB,
        CBC,
        CTR,
    }

    private final String keyAlgo = "AES";
    private final int blockSize = 16;
    private Mode mode;
    private Padding padding;
    private int fixedKeySize;

    private CipherContextRef pCtx = null;
    private byte[] keyValue;
    protected byte[] iv;
    private boolean initialized = false;
    private boolean encrypt = false;
    private int bytesBuffered = 0;

    private boolean calledUpdate;
    private String cipherName;

    private static final PublicKey constructPublicKey(byte[] encodedKey, String encodedKeyAlgorithm) throws NoSuchAlgorithmException, InvalidKeyException {
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

    private static final PrivateKey constructPrivateKey(byte[] encodedKey,
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

    private static final SecretKey constructSecretKey(byte[] encodedKey, String encodedKeyAlgorithm) {
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

    KAEAESCipher(Mode mode, Padding padding, int fixedKeySize) {
        this.mode = mode;
        this.padding = padding;
        this.fixedKeySize = fixedKeySize;
    }

    private static class CipherContextRef extends PhantomReference<KAEAESCipher> implements Comparable<CipherContextRef> {

        private static ReferenceQueue<KAEAESCipher> refQueue = new ReferenceQueue<>();
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

        CipherContextRef(KAEAESCipher kaeCipher, long ctxAddress) {
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
    protected void engineSetMode(String modeStr) throws NoSuchAlgorithmException {
        if (modeStr == null) {
            throw new NoSuchAlgorithmException("null mode");
        }

        if (modeStr.equalsIgnoreCase("ECB")) {
            mode = Mode.ECB;
        } else if (modeStr.equalsIgnoreCase("CBC")) {
            mode = Mode.CBC;
        } else if (modeStr.equalsIgnoreCase("CTR")) {
            mode = Mode.CTR;
        } else {
            throw new NoSuchAlgorithmException("Unsupported mode " + mode);
        }

    }

    @Override
    protected void engineSetPadding(String paddingStr) throws NoSuchPaddingException {
        if (paddingStr == null) {
            throw new NoSuchPaddingException("null padding");
        }

        if (paddingStr.equalsIgnoreCase("NOPADDING")) {
            this.padding = Padding.NOPADDING;
        } else if(paddingStr.equalsIgnoreCase("PKCS5PADDING")) {
            if (mode == Mode.CTR) {
                throw new NoSuchPaddingException("PKCS#5 padding not supported with CTR mode");
            }
            this.padding = Padding.PKCS5PADDING;
        } else {
            throw new NoSuchPaddingException("Unsupported padding "+padding);
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
        IvParameterSpec ivSpec = new IvParameterSpec(iv.clone());
        try {
            AlgorithmParameters params = AlgorithmParameters.getInstance(keyAlgo);
            params.init(ivSpec);
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
        AlgorithmParameterSpec ivSpec = null;
        if (params != null) {
            try {
                ivSpec = params.getParameterSpec(IvParameterSpec.class);
            } catch (InvalidParameterSpecException e) {
                throw new InvalidAlgorithmParameterException("Could not decode IV", e);
            }
        }
        engineInit(opmode, key, ivSpec, random);
    }

    @Override
    protected void engineInit(int opmode, Key key, AlgorithmParameterSpec params,
                    SecureRandom random) throws InvalidKeyException, InvalidAlgorithmParameterException {
        checkKey(key);
        boolean doEncrypt = (opmode == Cipher.ENCRYPT_MODE || opmode == Cipher.WRAP_MODE);

        byte[] ivBytes = null;

        if (params != null) {
            if (!(params instanceof IvParameterSpec)) {
                throw new InvalidKeyException("IvParameterSpec required. Received: " + params.getClass().getName());
            } else {
                ivBytes = ((IvParameterSpec) params).getIV();
                if (ivBytes.length != blockSize) {
                    throw new InvalidAlgorithmParameterException("Wrong IV length: must be " + blockSize +
                                    " bytes long. Received length:" + ivBytes.length);
                }
            }
        }
        if (mode == Mode.ECB) {
            if (params != null) {
                throw new InvalidAlgorithmParameterException("No Parameters for ECB mode");
            }
        } else if (ivBytes == null) {
            if (doEncrypt) {
                ivBytes = new byte[blockSize];
                if (random == null) {
                    random = JCAUtil.getSecureRandom();
                }
                random.nextBytes(ivBytes);
            } else {
                throw new InvalidAlgorithmParameterException("Parameters required for decryption");
            }
        }
        implInit(doEncrypt, key.getEncoded(), ivBytes);
    }

    private void implInit(boolean encrypt, byte[] keyVal, byte[] ivVal) {

        reset(true);
        this.encrypt = encrypt;
        this.keyValue = keyVal;
        this.iv = ivVal;
        this.cipherName = "aes-" + (keyVal.length * 8) + "-" + mode.toString().toLowerCase(Locale.US);

        // OpenSSL only supports PKCS5 Padding
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

    final int checkKey(Key key) throws InvalidKeyException {
        if (key == null || key.getEncoded() == null) {
            throw new InvalidKeyException("Key cannot be null");
        } else {
            if (!keyAlgo.equalsIgnoreCase(key.getAlgorithm())) {
                throw new InvalidKeyException("Key algorithm must be " + keyAlgo);
            }
            int keyLen = key.getEncoded().length;
            if (fixedKeySize == -1) {
                if (keyLen != 16 && keyLen != 24 & keyLen != 32) {
                    throw new InvalidKeyException("Key size is not valid. Got key length of: " + keyLen);
                }
            } else {
                if (keyLen != fixedKeySize) {
                    throw new InvalidKeyException("Only " + fixedKeySize + "-byte keys are accepted. Got: " + keyLen);
                }
            }
            return keyLen;
        }
    }

    @Override
    protected byte[] engineUpdate(byte[] input, int inputOffset, int inputLen) {
        byte[] out = new byte[getOutputSizeByOperation(inputLen, false)];
        int n = implUpdate(input, inputOffset, inputLen, out, 0);
        if (n == 0) {
            return new byte[0];
        } else if (out.length != n) {
            out = Arrays.copyOf(out, n);
        }
        return out;
    }

    @Override
    protected int engineUpdate(byte[] input, int inputOffset, int inputLen, byte[] output,
                    int outputOffset) throws ShortBufferException {
        int min = getOutputSizeByOperation(inputLen, false);
        if (output.length - outputOffset < min) {
            throw new ShortBufferException("min" + min + "-byte buffer needed");
        }
        return implUpdate(input, inputOffset, inputLen, output, outputOffset);
    }

    private int implUpdate(byte[] in, int inOfs, int inLen, byte[] output, int outOfs) {
        ensureInitialized();
        if (inLen <= 0) {
            return 0;
        }
        int k;
        try {
            k = nativeUpdate(pCtx.ctxAddress, in, inOfs, inLen, output, outOfs);
        } catch (ArrayIndexOutOfBoundsException e) {
            reset(true);
            throw new ProviderException("Invoke nativeUpdate failed for " + cipherName, e);
        }
        bytesBuffered += (inLen - k);

        calledUpdate = true;
        return k;
    }

    protected int getOutputSizeByOperation(int inLen, boolean isDoFinal) {
        if (inLen <= 0) {
            inLen = 0;
        }
        if (!isDoFinal && inLen == 0) {
            return 0;
        }
        if (padding == Padding.NOPADDING) {
            return inLen + bytesBuffered;
        } else {
            int len = inLen + bytesBuffered;

            /*
             * The amount of data written may be anything from zero bytes to (inl + cipher_block_size - 1) for encrypt.
             * Refer to {@link https://www.openssl.org/docs/man1.1.0/man3/EVP_CipherUpdate.html} for details.
             */
            len += (len % blockSize != 0 || encrypt) ? blockSize : 0;
            return len - (len % blockSize);
        }
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
        if (output.length - outputOffset < min) {
            throw new ShortBufferException("min" + min + "-byte buffer needed");
        }
        if (inputLen > 0) {
            outLen = implUpdate(input, inputOffset, inputLen, output, outputOffset);
            outputOffset += outLen;
        }
        outLen += implDoFinal(output, outputOffset);
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

    private int implDoFinal(byte[] out, int outOfs) {

        if (!encrypt && !calledUpdate) {
            return 0;
        }
        ensureInitialized();

        int outLen;
        try {
            outLen = nativeFinal(pCtx.ctxAddress, out, outOfs);
        } catch (ArrayIndexOutOfBoundsException | BadPaddingException e) {
            throw new ProviderException("Invoke nativeFinal failed for " + cipherName, e);
        } finally {
            reset(true);
        }

        return outLen;
    }

    protected void reset(boolean doCancel) {
        initialized = false;
        bytesBuffered = 0;
        calledUpdate = false;
        if (pCtx != null) {
            pCtx.dispose(doCancel);
            pCtx = null;
        }
    }

    protected native static long nativeInit(String cipherType, boolean encrypt, byte[] key, byte[] iv, boolean padding) throws RuntimeException;

    protected native static int nativeUpdate(long pContext, byte[] in, int inOfs, int inLen, byte[] out,
                    int outOfs) throws ArrayIndexOutOfBoundsException;

    protected native static int nativeFinal(long pContext, byte[] out,
                    int outOfs) throws ArrayIndexOutOfBoundsException, BadPaddingException;

    protected native static void nativeFree(long pContext);

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

