/*
 * Copyright (c) 1997, 2020, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2021, Huawei Technologies Co., Ltd. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
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

import javax.crypto.BadPaddingException;
import java.security.SignatureException;

public class KAERSASignatureNative {
    // rsa sign
    protected static native byte[] rsaSign(long keyAddress, String digestName, byte[] digestBytes, int paddingType)
        throws SignatureException;

    // rsa verify
    protected static native boolean rsaVerify(long keyAddress, String digestName, byte[] digestBytes, int paddingType,
        byte[] sigBytes) throws SignatureException, BadPaddingException;

    // rsa pss sign
    protected static native byte[] pssSign(long keyAddress, String digestName, byte[] digestBytes, int paddingType,
        String mgf1DigestName, int saltLen) throws SignatureException;

    // rsa pss verify
    protected static native boolean pssVerify(long keyAddress, String digestName, byte[] digestBytes, int paddingType,
        String mgf1DigestName, int saltLen, byte[] sigBytes) throws SignatureException, BadPaddingException;
}
