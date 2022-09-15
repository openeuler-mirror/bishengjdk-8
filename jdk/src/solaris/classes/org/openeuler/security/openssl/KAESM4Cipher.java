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

import sun.security.util.Debug;

import java.nio.ByteBuffer;
import java.security.InvalidAlgorithmParameterException;
import java.security.InvalidKeyException;
import java.security.NoSuchAlgorithmException;
import java.security.Key;
import java.security.ProviderException;
import java.util.Locale;

import javax.crypto.BadPaddingException;
import javax.crypto.IllegalBlockSizeException;
import javax.crypto.NoSuchPaddingException;
import javax.crypto.ShortBufferException;

/*
 * This class currently supports:
 * - SM4/ECB/NOPADDING
 * - SM4/ECB/PKCS5PADDING
 * - SM4/CBC/NOPADDING
 * - SM4/CBC/PKCS5PADDING
 * - SM4/CTR/NOPADDING
 * - SM4/OFB/NOPADDING
 * - SM4/OFB/PKCS5PADDING
 */
abstract class KAESM4Cipher extends KAESymmetricCipherBase {

    private static final Debug debug = Debug.getInstance("kae");

    /*
     * SM4 max chunk size of each encryption or decryption
     * when input data does not have an accessible byte[]
     */
    private static final int DEFAULT_KAE_SM4_MAX_CHUNK_SIZE = 4096;
    private static int KAE_SM4_MAX_CHUNK_SIZE;
    static {
        initSM4MaxChunkSize();
    }

    private static void initSM4MaxChunkSize() {
        String maxChunkSize = KAEConfig.privilegedGetOverridable("kae.sm4.maxChunkSize",
                DEFAULT_KAE_SM4_MAX_CHUNK_SIZE + "");
        try {
            KAE_SM4_MAX_CHUNK_SIZE = Integer.parseInt(maxChunkSize);
        } catch (NumberFormatException e) {
            // When parsing string argument to signed decimal integer fails, uses the default chunk size (4096)
            KAE_SM4_MAX_CHUNK_SIZE = DEFAULT_KAE_SM4_MAX_CHUNK_SIZE;
            if (debug != null) {
                debug.println("The configured block size (" + maxChunkSize + ") cannot be converted to an integer, " +
                        "uses the default chunk size (" + DEFAULT_KAE_SM4_MAX_CHUNK_SIZE + ")");
                e.printStackTrace();
            }
            return;
        }
        // when the configured chunk size is less than or equal to 0, uses the default chunk size (4096)
        if (KAE_SM4_MAX_CHUNK_SIZE <= 0) {
            KAE_SM4_MAX_CHUNK_SIZE = DEFAULT_KAE_SM4_MAX_CHUNK_SIZE;
            if (debug != null) {
                debug.println("The configured chunk size (" + KAE_SM4_MAX_CHUNK_SIZE + ") is less than " +
                        "or equal to 0, uses the default chunk size (" + DEFAULT_KAE_SM4_MAX_CHUNK_SIZE + ")");
            }
            return;
        }
        if (debug != null) {
            debug.println("The configured chunk size is " + KAE_SM4_MAX_CHUNK_SIZE);
        }
    }

    /**
     * Used by the engineUpdate(ByteBuffer, ByteBuffer) and
     * engineDoFinal(ByteBuffer, ByteBuffer) methods.
     */
    private static int getSM4MaxChunkSize(int totalSize) {
        return Math.min(KAE_SM4_MAX_CHUNK_SIZE, totalSize);
    }

    public static class Sm4 extends KAESM4Cipher {
        public Sm4(Mode mode, Padding padding) {
            super(mode, padding, 16);
        }

        public static class Cbc extends Sm4 {
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

        public static class Ecb extends Sm4 {
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

        public static class Ctr extends Sm4 {
            public Ctr(Padding padding) {
                super(Mode.CTR, padding);
            }
            public static class NoPadding extends Ctr {
                public NoPadding() {
                    super(Padding.NOPADDING);
                }
            }
        }

        public static class Ofb extends Sm4 {
            public Ofb(Padding padding) {
                super(Mode.OFB, padding);
            }
            public static class NoPadding extends Ofb {
                public NoPadding() {
                    super(Padding.NOPADDING);
                }
            }
            public static class PKCS5Padding extends Ofb {
                public PKCS5Padding() {
                    super(Padding.PKCS5PADDING);
                }
            }
        }
    }

    KAESM4Cipher(Mode mode, Padding padding, int fixedKeySize) {
        super(mode, padding, fixedKeySize, "SM4");
    }

    protected void checkKey(Key key) throws InvalidKeyException {
        if (key == null || key.getEncoded() == null) {
            throw new InvalidKeyException("Key cannot be null");
        } else {
            int keyLen = key.getEncoded().length;
            if (keyLen != fixedKeySize) {
                throw new InvalidKeyException("Only " + fixedKeySize + "-byte keys are accepted. Got: " + keyLen);
            }
        }
    }

    protected String getCipherName(int keyLength, Mode mode) {
        return "sm4" + "-" + mode.toString().toLowerCase(Locale.US);
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
        } else if (modeStr.equalsIgnoreCase("OFB")) {
            mode = Mode.OFB;
        } else {
            throw new NoSuchAlgorithmException("Unsupported mode " + mode);
        }
    }

    @Override
    protected void engineSetPadding(String paddingStr) throws NoSuchPaddingException {
        if (paddingStr == null) {
            throw new NoSuchPaddingException("null padding");
        }
        if (paddingStr.equalsIgnoreCase("PKCS7PADDING")) {
            paddingStr = "PKCS5Padding";
        }

        if (paddingStr.equalsIgnoreCase("NOPADDING")) {
            this.padding = Padding.NOPADDING;
        } else if(paddingStr.equalsIgnoreCase("PKCS5PADDING")) {
            if (mode == Mode.CTR) {
                throw new NoSuchPaddingException("PKCS#5 padding not supported with CTR mode");
            }
            this.padding = Padding.PKCS5PADDING;
        } else {
           throw new NoSuchPaddingException("Unsupported padding " + paddingStr);
        }
    }

    @Override
    protected int engineUpdate(ByteBuffer input, ByteBuffer output) throws ShortBufferException {
        try {
            return bufferCrypt(input, output, true);
        } catch (IllegalBlockSizeException e) {
            // never thrown for engineUpdate()
            throw new ProviderException("Internal error in update()");
        } catch (BadPaddingException e) {
            // never thrown for engineUpdate()
            throw new ProviderException("Internal error in update()");
        }
    }

    @Override
    protected int engineDoFinal(ByteBuffer input, ByteBuffer output)
            throws ShortBufferException, IllegalBlockSizeException, BadPaddingException {
        return bufferCrypt(input, output, false);
    }

    /**
     * Implementation for encryption using ByteBuffers. Used for both
     * engineUpdate() and engineDoFinal().
     */
    private int bufferCrypt(ByteBuffer input, ByteBuffer output,
                            boolean isUpdate) throws ShortBufferException,
            IllegalBlockSizeException, BadPaddingException {
        if ((input == null) || (output == null)) {
            throw new NullPointerException
                    ("Input and output buffers must not be null");
        }
        int inPos = input.position();
        int inLimit = input.limit();
        int inLen = inLimit - inPos;
        if (isUpdate && (inLen == 0)) {
            return 0;
        }
        int outLenNeeded = engineGetOutputSize(inLen);

        if (output.remaining() < outLenNeeded) {
            throw new ShortBufferException("Need at least " + outLenNeeded
                    + " bytes of space in output buffer");
        }

        // detecting input and output buffer overlap may be tricky
        // we can only write directly into output buffer when we
        // are 100% sure it's safe to do so

        boolean a1 = input.hasArray();
        boolean a2 = output.hasArray();
        int total = 0;

        if (a1) { // input has an accessible byte[]
            byte[] inArray = input.array();
            int inOfs = input.arrayOffset() + inPos;

            byte[] outArray;
            if (a2) { // output has an accessible byte[]
                outArray = output.array();
                int outPos = output.position();
                int outOfs = output.arrayOffset() + outPos;

                // check array address and offsets and use temp output buffer
                // if output offset is larger than input offset and
                // falls within the range of input data
                boolean useTempOut = false;
                if (inArray == outArray &&
                        ((inOfs < outOfs) && (outOfs < inOfs + inLen))) {
                    useTempOut = true;
                    outArray = new byte[outLenNeeded];
                    outOfs = 0;
                }
                if (isUpdate) {
                    total = engineUpdate(inArray, inOfs, inLen, outArray, outOfs);
                } else {
                    total = engineDoFinal(inArray, inOfs, inLen, outArray, outOfs);
                }
                if (useTempOut) {
                    output.put(outArray, outOfs, total);
                } else {
                    // adjust output position manually
                    output.position(outPos + total);
                }
            } else { // output does not have an accessible byte[]
                if (isUpdate) {
                    outArray = engineUpdate(inArray, inOfs, inLen);
                } else {
                    outArray = engineDoFinal(inArray, inOfs, inLen);
                }
                if (outArray != null && outArray.length != 0) {
                    output.put(outArray);
                    total = outArray.length;
                }
            }
            // adjust input position manually
            input.position(inLimit);
        } else { // input does not have an accessible byte[]
            // have to assume the worst, since we have no way of determine
            // if input and output overlaps or not
            byte[] tempOut = new byte[outLenNeeded];
            int outOfs = 0;

            byte[] tempIn = new byte[getSM4MaxChunkSize(inLen)];
            do {
                int chunk = Math.min(inLen, tempIn.length);
                if (chunk > 0) {
                    input.get(tempIn, 0, chunk);
                }
                int n;
                if (isUpdate || (inLen > chunk)) {
                    n = engineUpdate(tempIn, 0, chunk, tempOut, outOfs);
                } else {
                    n = engineDoFinal(tempIn, 0, chunk, tempOut, outOfs);
                }
                outOfs += n;
                total += n;
                inLen -= chunk;
            } while (inLen > 0);
            if (total > 0) {
                output.put(tempOut, 0, total);
            }
        }

        return total;
    }

    protected void checkIvBytes(byte[] ivBytes) throws InvalidAlgorithmParameterException {
        if (ivBytes == null) {
            throw new InvalidAlgorithmParameterException("Wrong IV length: iv is null ");
        }
        if (mode == Mode.CTR) {
            if (ivBytes.length < 8) {
                throw new InvalidAlgorithmParameterException("Wrong IV length: CTR mode requires IV of at least: 8 bytes.");
            }
            return;
        }
        if (ivBytes.length != blockSize) {
            throw new InvalidAlgorithmParameterException("Wrong IV length: must be " + blockSize + " bytes long.");
        }
    }
}

