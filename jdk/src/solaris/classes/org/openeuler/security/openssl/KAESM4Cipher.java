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
 * - SM4/ECB/NOPADDING
 * - SM4/ECB/PKCS5PADDING
 * - SM4/CBC/NOPADDING
 * - SM4/CBC/PKCS5PADDING
 * - SM4/CTR/NOPADDING
 * - SM4/OFB/NOPADDING
 * - SM4/OFB/PKCS5PADDING
 */
abstract class KAESM4Cipher extends KAESymmetricCipherBase {

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

