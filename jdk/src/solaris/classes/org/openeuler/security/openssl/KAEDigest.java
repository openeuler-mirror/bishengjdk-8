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

import java.lang.ref.PhantomReference;
import java.lang.ref.ReferenceQueue;
import java.security.DigestException;
import java.security.MessageDigestSpi;
import java.security.ProviderException;
import java.util.Set;
import java.util.concurrent.ConcurrentSkipListSet;

/**
 * KAE Digest
 */
abstract class KAEDigest extends MessageDigestSpi implements Cloneable {

    public static final class MD5 extends KAEDigest {
        private static final long initContext = nativeInit("md5");

        public MD5() {
            super("md5", 16, initContext);
        }
    }

    public static final class SM3 extends KAEDigest {
        private static final long initContext = nativeInit("sm3");

        public SM3() {
            super("sm3", 32, initContext);
        }
    }

    public static final class SHA256 extends KAEDigest {
        private static final long initContext = nativeInit("sha256");

        public SHA256() {
            super("sha256", 32, initContext);
        }
    }

    public static final class SHA384 extends KAEDigest {
        private static final long initContext = nativeInit("sha384");

        public SHA384() {
            super("sha384", 48, initContext);
        }
    }

    private final int digestLength;

    private final String algorithm;
    private final long initContext;

    // field for ensuring native memory is freed
    private DigestContextRef contextRef = null;

    KAEDigest(String algorithm, int digestLength, long initContext) {
        this.algorithm = algorithm;
        this.digestLength = digestLength;
        this.initContext = initContext;
    }

    private static class DigestContextRef extends PhantomReference<KAEDigest>
        implements Comparable<DigestContextRef> {

        private static ReferenceQueue<KAEDigest> referenceQueue = new ReferenceQueue<>();
        private static Set<DigestContextRef> referenceList = new ConcurrentSkipListSet<>();
        private static boolean disableKaeDispose = Boolean.getBoolean("kae.disableKaeDispose");

        private final long ctxAddress;

        DigestContextRef(KAEDigest kaeDigest, long ctxAddress) {
            super(kaeDigest, referenceQueue);
            this.ctxAddress = ctxAddress;
            if (!disableKaeDispose) {
                referenceList.add(this);
                drainRefQueueBounded();
            }
        }

        @Override
        public int compareTo(DigestContextRef other) {
            if (this.ctxAddress == other.ctxAddress) {
                return 0;
            } else {
                return (this.ctxAddress < other.ctxAddress) ? -1 : 1;
            }
        }

        private static void drainRefQueueBounded() {
            while (true) {
                DigestContextRef next = (DigestContextRef) referenceQueue.poll();
                if (next == null) {
                    break;
                }
                next.dispose();
            }
        }

        void dispose() {
            if (!disableKaeDispose) {
                referenceList.remove(this);
                try {
                    nativeFree(ctxAddress);
                } finally {
                    this.clear();
                }
            } else {
                nativeFree(ctxAddress);
            }
        }
    }

    // single byte update. See JCA doc.
    @Override
    protected synchronized void engineUpdate(byte input) {
        byte[] oneByte = new byte[]{input};
        engineUpdate(oneByte, 0, 1);
    }


    // array update. See JCA doc.
    @Override
    protected synchronized void engineUpdate(byte[] input, int offset, int len) {
        if (len == 0 || input == null) {
            return;
        }
        if ((offset < 0) || (len < 0) || (offset > input.length - len)) {
            throw new ArrayIndexOutOfBoundsException();
        }
        if (contextRef == null) {
            contextRef = createDigestContext(this);
        }

        try {
            nativeUpdate(contextRef.ctxAddress, input, offset, len);
        } catch (Exception e) {
            engineReset();
            throw new ProviderException("nativeUpdate failed for " + algorithm, e);
        }
    }


    // return the digest. See JCA doc.
    @Override
    protected synchronized byte[] engineDigest() {
        final byte[] output = new byte[digestLength];
        try {
            engineDigest(output, 0, digestLength);
        } catch (Exception e) {
            throw new ProviderException("Internal error", e);
        }
        return output;
    }

    // return the digest in the specified array. See JCA doc.
    @Override
    protected int engineDigest(byte[] output, int offset, int len) throws DigestException {
        if (output == null) {
            return 0;
        }
        if (len < digestLength) {
            throw new DigestException("Length must be at least "
                    + digestLength + " for " + algorithm + " digests");
        }
        if ((offset < 0) || (len < 0) || (offset > output.length - len)) {
            throw new DigestException("Buffer too short to store digest");
        }
        if (contextRef == null) {
            contextRef = createDigestContext(this);
        }
        try {
            nativeDigest(contextRef.ctxAddress, output, offset, digestLength);
        } catch (Exception e) {
            throw new ProviderException("Invoke nativeDigest failed for " + algorithm, e);
        } finally {
            engineReset();
        }
        return digestLength;
    }

    // reset this object. See JCA doc.
    @Override
    protected synchronized void engineReset() {
        if (contextRef != null) {
            contextRef.dispose();
            contextRef = null;
        }
    }

    // return digest length. See JCA doc.
    @Override
    protected int engineGetDigestLength() {
        return digestLength;
    }

    @Override
    public synchronized Object clone() throws CloneNotSupportedException {
        KAEDigest kaeDigest = (KAEDigest) super.clone();
        if (kaeDigest.contextRef != null && kaeDigest.contextRef.ctxAddress != 0) {
            long addr;
            try {
                addr = nativeClone(kaeDigest.contextRef.ctxAddress);
            } catch (Exception e) {
                throw new ProviderException("Invoke nativeClone failed for " + algorithm, e);
            }
            kaeDigest.contextRef = new DigestContextRef(kaeDigest, addr);
        }
        return kaeDigest;
    }

    private DigestContextRef createDigestContext(KAEDigest kaeDigest) {
        long addr;
        try {
            addr = nativeClone(initContext);
        } catch (Exception e) {
            throw new ProviderException("Invoke nativeInit failed for " + algorithm, e);
        }
        if (addr == 0) {
            throw new RuntimeException("Cannot initialize EVP_MD_CTX for " + algorithm);
        }
        return new DigestContextRef(kaeDigest, addr);
    }

    // return pointer to the context
    protected native static long nativeInit(String algorithmName);

    // update the input byte
    protected native static void nativeUpdate(long ctxAddress, byte[] input, int offset, int inLen);

    // digest and store the digest message to output
    protected native static int nativeDigest(long ctxAddress, byte[] output, int offset, int len);

    // digest clone
    protected static native long nativeClone(long ctxAddress);

    // free the specified context
    protected native static void nativeFree(long ctxAddress);
}
