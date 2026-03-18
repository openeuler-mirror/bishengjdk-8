/*
 * Copyright (c) 2024, Huawei Technologies Co., Ltd. All rights reserved.
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

#ifndef OPENSSL1_MACRO_H
#define OPENSSL1_MACRO_H

#define SSL1_EVP_PKEY_OP_UNDEFINED 0
#define SSL1_EVP_PKEY_OP_PARAMGEN (1 << 1)
#define SSL1_EVP_PKEY_OP_KEYGEN (1 << 2)
#define SSL1_EVP_PKEY_OP_SIGN (1 << 3)
#define SSL1_EVP_PKEY_OP_VERIFY (1 << 4)
#define SSL1_EVP_PKEY_OP_VERIFYRECOVER (1 << 5)
#define SSL1_EVP_PKEY_OP_SIGNCTX (1 << 6)
#define SSL1_EVP_PKEY_OP_VERIFYCTX (1 << 7)
#define SSL1_EVP_PKEY_OP_ENCRYPT (1 << 8)
#define SSL1_EVP_PKEY_OP_DECRYPT (1 << 9)
#define SSL1_EVP_PKEY_OP_DERIVE (1 << 10)

#define SSL1_EVP_PKEY_ALG_CTRL 0x1000
#define SSL1_EVP_PKEY_CTRL_RSA_PADDING (SSL1_EVP_PKEY_ALG_CTRL + 1)
#define SSL1_EVP_PKEY_OP_TYPE_SIG                                                                                  \
    (SSL1_EVP_PKEY_OP_SIGN | SSL1_EVP_PKEY_OP_VERIFY | SSL1_EVP_PKEY_OP_VERIFYRECOVER | SSL1_EVP_PKEY_OP_SIGNCTX | \
        SSL1_EVP_PKEY_OP_VERIFYCTX)
#define SSL1_EVP_PKEY_CTRL_MD 1
#define SSL1_EVP_PKEY_OP_TYPE_CRYPT (SSL1_EVP_PKEY_OP_ENCRYPT | SSL1_EVP_PKEY_OP_DECRYPT)
#define SSL1_EVP_PKEY_CTRL_RSA_MGF1_MD (SSL1_EVP_PKEY_ALG_CTRL + 5)
#define SSL1_EVP_PKEY_CTRL_RSA_PSS_SALTLEN (SSL1_EVP_PKEY_ALG_CTRL + 2)
#define SSL1_EVP_PKEY_CTRL_RSA_OAEP_LABEL (SSL1_EVP_PKEY_ALG_CTRL + 10)
#define SSL1_EVP_PKEY_CTRL_RSA_OAEP_MD (SSL1_EVP_PKEY_ALG_CTRL + 9)
#define SSL1_EVP_PKEY_CTRL_SET1_ID (SSL1_EVP_PKEY_ALG_CTRL + 11)

#define SSL1_NID_rsaEncryption 6
#define SSL1_EVP_PKEY_RSA SSL1_NID_rsaEncryption

#endif // OPENSSL1_MACRO_H