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

import java.security.InvalidAlgorithmParameterException;
import java.security.InvalidKeyException;
import java.security.NoSuchAlgorithmException;
import java.security.Key;
import java.util.Locale;

import javax.crypto.NoSuchPaddingException;

/*
 * This class currently supports:
 * - AES/ECB/NOPADDING
 * - AES/ECB/PKCS5PADDING
 * - AES/CBC/NOPADDING
 * - AES/CBC/PKCS5PADDING
 * - AES/CTR/NOPADDING
 * - AES/GCM/NOPADDING
 */
abstract class KAEAESCipher extends KAESymmetricCipherBase {

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

        public static class Gcm extends Aes {
            public Gcm(Padding padding) {
                super(Mode.GCM, padding);
            }
            public static class NoPadding extends Gcm {
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

        public static class Gcm extends Aes_128 {
            public Gcm(Padding padding) {
                super(Mode.GCM, padding);
            }
            public static class NoPadding extends Gcm {
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

        public static class Gcm extends Aes_192 {
            public Gcm(Padding padding) {
                super(Mode.GCM, padding);
            }
            public static class NoPadding extends Gcm {
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

        public static class Gcm extends Aes_256 {
            public Gcm(Padding padding) {
                super(Mode.GCM, padding);
            }
            public static class NoPadding extends Gcm {
                public NoPadding() {
                    super(Padding.NOPADDING);
                }
            }
        }
    }

    KAEAESCipher(Mode mode, Padding padding, int fixedKeySize) {
        super(mode, padding, fixedKeySize, "AES");
    }

    protected void checkKey(Key key) throws InvalidKeyException {
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
        }
    }

    protected String getCipherName(int keyLength, Mode mode) {
        return "aes-" + keyLength + "-" + mode.toString().toLowerCase(Locale.US);
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
        } else if (modeStr.equalsIgnoreCase("GCM")) {
            mode = Mode.GCM;
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

        // GCM has no Padding, pkcs5-> nopadding in sunjce
        if (mode == Mode.GCM) {
            this.padding = Padding.NOPADDING;
        } else if (paddingStr.equalsIgnoreCase("NOPADDING")) {
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

    protected void checkIvBytes(byte[] ivBytes) throws InvalidAlgorithmParameterException {
        if ((ivBytes == null) || (ivBytes.length != blockSize)) {
            throw new InvalidAlgorithmParameterException("Wrong IV length: must be " + blockSize + " bytes long.");
        }
    }
}

