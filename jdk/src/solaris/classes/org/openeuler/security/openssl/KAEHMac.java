/*
 * Copyright (c) 2014, Oracle and/or its affiliates. All rights reserved.
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

import javax.crypto.MacSpi;
import javax.crypto.SecretKey;
import java.lang.ref.PhantomReference;
import java.lang.ref.ReferenceQueue;
import java.nio.ByteBuffer;
import java.security.*;
import java.security.spec.AlgorithmParameterSpec;
import java.util.Set;
import java.util.concurrent.ConcurrentSkipListSet;

public abstract class KAEHMac extends MacSpi {

    private final String algorithm;

    /**
     * The secret key used in this keyed HMAC.
     */
    private byte[] keyBytes;

    /**
     * Holds the output size of the message digest.
     */
    private final int digestSize;

    /**
     * Holds a dummy buffer for writing single bytes to the digest.
     */
    private final byte[] singleByte = new byte[1];

    private HmacContextRef contextRef = null;

    private KAEHMac(String algo, int size) {
        this.algorithm = algo;
        this.digestSize = size;
    }

    private static class HmacContextRef extends PhantomReference<KAEHMac>
            implements Comparable<HmacContextRef> {

        private static ReferenceQueue<KAEHMac> referenceQueue = new ReferenceQueue<>();
        private static Set<HmacContextRef> referenceList = new ConcurrentSkipListSet<>();
        private static boolean disableKaeDispose = Boolean.getBoolean("kae.disableKaeDispose");

        private final long address;

        HmacContextRef(KAEHMac kaeHMac, long address) {
            super(kaeHMac, referenceQueue);
            this.address = address;
            if (!disableKaeDispose) {
                referenceList.add(this);
                drainRefQueueBounded();
            }
        }

        @Override
        public int compareTo(HmacContextRef other) {
            if (this.address == other.address) {
                return 0;
            } else {
                return (this.address < other.address) ? -1 : 1;
            }
        }

        private static void drainRefQueueBounded() {
            while (true) {
                HmacContextRef next = (HmacContextRef) referenceQueue.poll();
                if (next == null) break;
                next.dispose(true);
            }
        }

        void dispose(boolean needFree) {
            if (!disableKaeDispose) {
                referenceList.remove(this);
                try {
                    if (needFree) {
                        nativeFree(address);
                    }
                } finally {
                    this.clear();
                }
            } else {
                nativeFree(address);
            }
        }
    }

    private void checkAndInitHmacContext () {
        try {
            if (contextRef == null) {
                long ctxAddr = nativeInit(keyBytes, keyBytes.length, algorithm);
                contextRef = new HmacContextRef(this, ctxAddr);
            }
        }
        catch (Exception e) {
            throw new ProviderException(e.getMessage()) ;
        }
    }

    @Override
    protected int engineGetMacLength() {
        return digestSize;
    }

    @Override
    protected void engineInit(Key key, AlgorithmParameterSpec params)
            throws InvalidKeyException, InvalidAlgorithmParameterException {
        if (!(key instanceof SecretKey)) {
            throw new InvalidKeyException("key must be a SecretKey");
        }
        if (params != null) {
            throw new InvalidAlgorithmParameterException("unknown parameter type");
        }
        keyBytes = key.getEncoded();
        if (keyBytes == null) {
            throw new InvalidKeyException("key cannot be encoded");
        }
    }

    @Override
    protected void engineUpdate(byte input) {
        singleByte[0] = input;
        engineUpdate(singleByte, 0, 1);
    }

    @Override
    protected void engineUpdate(byte[] input, int offset, int len) {
        checkAndInitHmacContext();
        try {
            nativeUpdate(contextRef.address, input, offset, len);
        }
        catch (Exception e) {
            engineReset();
            throw new ProviderException(e.getMessage());
        }
    }


    @Override
    protected byte[] engineDoFinal() {
        final byte[] output = new byte[digestSize];
        checkAndInitHmacContext();
        final byte[] res;
        try {
            int bytesWritten = nativeFinal(contextRef.address, output, 0, digestSize);
            res = new byte[bytesWritten];
            System.arraycopy(output, 0, res, 0, bytesWritten);
        }
        catch (Exception e) {
            engineReset();
            throw new ProviderException(e.getMessage());
        }
        return res;
    }

    @Override
    protected void engineReset() {
        if (contextRef != null) {
            contextRef.dispose(true);
            contextRef = null;
        }
    }

    public static final class HmacMD5 extends KAEHMac {
        public HmacMD5() {
            super("MD5", 16);
        }
    }
    public static final class HmacSHA1 extends KAEHMac {
        public HmacSHA1() {
            super("SHA1", 20);
        }
    }
    public static final class HmacSHA224 extends KAEHMac {
        public HmacSHA224() throws NoSuchAlgorithmException {
            super("SHA224", 28);
        }
    }
    public static final class HmacSHA256 extends KAEHMac {
        public HmacSHA256() throws NoSuchAlgorithmException {
            super("SHA256", 32);
        }
    }
    public static final class HmacSHA384 extends KAEHMac {
        public HmacSHA384() throws NoSuchAlgorithmException {
            super("SHA384", 48);
        }
    }
    public static final class HmacSHA512 extends KAEHMac {
        public HmacSHA512() throws NoSuchAlgorithmException {
            super("SHA512", 64);
        }
    }
    
    protected static native long nativeInit(byte[] key, int len, String algo);

    protected static native void nativeUpdate(long ctxAddr, byte[] input, int inOffset, int inLen);

    protected static native int nativeFinal(long ctxAddr, byte[] output, int outOffset, int inLen);

    protected static native void nativeFree(long ctxAddr);
}
