/*
 * Copyright (c) 1999, 2013, Oracle and/or its affiliates. All rights reserved.
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

import javax.crypto.Cipher;
import javax.crypto.SecretKey;
import javax.crypto.spec.SecretKeySpec;
import java.security.*;
import java.security.spec.InvalidKeySpecException;
import java.security.spec.PKCS8EncodedKeySpec;
import java.security.spec.X509EncodedKeySpec;
import java.util.*;

class KAEUtils {
    enum MessageDigestType {
        MD2("MD2", "md2", 16),
        MD5("MD5", "md5", 16),
        SHA1("SHA-1", "sha1", 20,
                new HashSet<>(Arrays.asList("SHA1", "1.3.14.3.2.26", "OID.1.3.14.3.2.26"))),
        SHA224("SHA-224", "sha224", 28,
                new HashSet<>(Arrays.asList("2.16.840.1.101.3.4.2.4", "OID.2.16.840.1.101.3.4.2.4"))),
        SHA256("SHA-256", "sha256", 32,
                new HashSet<>(Arrays.asList("2.16.840.1.101.3.4.2.1", "OID.2.16.840.1.101.3.4.2.1"))),
        SHA384("SHA-384", "sha384", 48,
                new HashSet<>(Arrays.asList("2.16.840.1.101.3.4.2.2", "OID.2.16.840.1.101.3.4.2.2"))),
        SHA512("SHA-512", "sha512", 64,
                new HashSet<>(Arrays.asList("2.16.840.1.101.3.4.2.3", "OID.2.16.840.1.101.3.4.2.3"))),
        SHA512_224("SHA-512/224", "sha512-224", 28,
                new HashSet<>(Arrays.asList("2.16.840.1.101.3.4.2.5", "OID.2.16.840.1.101.3.4.2.5"))),
        SHA_512_256("SHA-512/256", "sha512-256", 32,
                new HashSet<>(Arrays.asList("2.16.840.1.101.3.4.2.6", "OID.2.16.840.1.101.3.4.2.6")));

        final String digestName;
        final String kaeDigestName;
        final int digestLen;
        final Set<String> aliasNames;

        public String getDigestName() {
            return digestName;
        }

        public String getKaeDigestName() {
            return kaeDigestName;
        }

        public int getDigestLen() {
            return digestLen;
        }

        public Set<String> getAliasNames() {
            return aliasNames;
        }

        MessageDigestType(String digestName, String kaeDigestName, int digestLen, Set<String> aliasNames) {
            this.digestName = digestName;
            this.kaeDigestName = kaeDigestName;
            this.digestLen = digestLen;
            this.aliasNames = aliasNames;
        }

        MessageDigestType(String digestName, String kaeDigestName, int digestLen) {
            this(digestName, kaeDigestName, digestLen, Collections.emptySet());
        }
    }

    /**
     * kae digest algorithm info map
     */
    private static final Map<String, String> DIGEST_ALGORITHM_NAME_MAP = new HashMap<>();
    private static final Map<String, Integer> DIGEST_ALGORITHM_LENGTH_MAP = new HashMap<>();

    static {
        initDigest();
    }

    private static void initDigest() {
        MessageDigestType[] messageDigestTypes = MessageDigestType.values();
        for (MessageDigestType messageDigestType : messageDigestTypes) {
            DIGEST_ALGORITHM_NAME_MAP.put(messageDigestType.getDigestName(), messageDigestType.getKaeDigestName());
            DIGEST_ALGORITHM_LENGTH_MAP.put(messageDigestType.getDigestName(), messageDigestType.getDigestLen());
            for (String aliasName : messageDigestType.getAliasNames()) {
                DIGEST_ALGORITHM_NAME_MAP.put(aliasName, messageDigestType.getKaeDigestName());
                DIGEST_ALGORITHM_LENGTH_MAP.put(aliasName, messageDigestType.getDigestLen());
            }
        }
    }

    // get the kae digest algorithm name
    static String getKAEDigestName(String digestName) {
        return DIGEST_ALGORITHM_NAME_MAP.get(digestName);
    }

    static int getDigestLength(String digestName) {
        return DIGEST_ALGORITHM_LENGTH_MAP.get(digestName);
    }

    static class ConstructKeys {
        /**
         * Construct a public key from its encoding.
         *
         * @param encodedKey          the encoding of a public key.
         * @param encodedKeyAlgorithm the algorithm the encodedKey is for.
         * @return a public key constructed from the encodedKey.
         */
        private static PublicKey constructPublicKey(byte[] encodedKey,
                                                    String encodedKeyAlgorithm)
                throws InvalidKeyException, NoSuchAlgorithmException {
            try {
                KeyFactory keyFactory =
                        KeyFactory.getInstance(encodedKeyAlgorithm);
                X509EncodedKeySpec keySpec = new X509EncodedKeySpec(encodedKey);
                return keyFactory.generatePublic(keySpec);
            } catch (NoSuchAlgorithmException nsae) {
                throw new NoSuchAlgorithmException("No installed providers " +
                        "can create keys for the " +
                        encodedKeyAlgorithm +
                        "algorithm", nsae);
            } catch (InvalidKeySpecException ike) {
                throw new InvalidKeyException("Cannot construct public key", ike);
            }
        }

        /**
         * Construct a private key from its encoding.
         *
         * @param encodedKey          the encoding of a private key.
         * @param encodedKeyAlgorithm the algorithm the wrapped key is for.
         * @return a private key constructed from the encodedKey.
         */
        private static PrivateKey constructPrivateKey(byte[] encodedKey,
                                                      String encodedKeyAlgorithm) throws InvalidKeyException,
                NoSuchAlgorithmException {
            try {
                KeyFactory keyFactory =
                        KeyFactory.getInstance(encodedKeyAlgorithm);
                PKCS8EncodedKeySpec keySpec = new PKCS8EncodedKeySpec(encodedKey);
                return keyFactory.generatePrivate(keySpec);
            } catch (NoSuchAlgorithmException nsae) {
                throw new NoSuchAlgorithmException("No installed providers " +
                        "can create keys for the " +
                        encodedKeyAlgorithm +
                        "algorithm", nsae);
            } catch (InvalidKeySpecException ike) {
                throw new InvalidKeyException("Cannot construct private key", ike);
            }
        }

        /**
         * Construct a secret key from its encoding.
         *
         * @param encodedKey          the encoding of a secret key.
         * @param encodedKeyAlgorithm the algorithm the secret key is for.
         * @return a secret key constructed from the encodedKey.
         */
        private static SecretKey constructSecretKey(byte[] encodedKey,
                                                    String encodedKeyAlgorithm) {
            return new SecretKeySpec(encodedKey, encodedKeyAlgorithm);
        }

        static Key constructKey(byte[] encoding, String keyAlgorithm,
                                int keyType) throws InvalidKeyException, NoSuchAlgorithmException {
            switch (keyType) {
                case Cipher.SECRET_KEY:
                    return constructSecretKey(encoding, keyAlgorithm);
                case Cipher.PRIVATE_KEY:
                    return constructPrivateKey(encoding, keyAlgorithm);
                case Cipher.PUBLIC_KEY:
                    return constructPublicKey(encoding, keyAlgorithm);
                default:
                    throw new InvalidKeyException("Unknown keytype " + keyType);
            }
        }
    }
}
