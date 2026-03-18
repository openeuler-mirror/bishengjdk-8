/*
 * Copyright (c) 2024, Huawei Technologies Co., Ltd. All rights reserved.
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
import java.lang.reflect.Field;
import java.nio.charset.StandardCharsets;
import java.security.*;
import java.security.interfaces.ECPrivateKey;
import java.security.interfaces.ECPublicKey;
import java.security.spec.AlgorithmParameterSpec;
import java.util.Set;
import java.util.concurrent.ConcurrentSkipListSet;

import static org.openeuler.security.openssl.KAEUtils.asUnsignedByteArray;

/**
 * We only support support SM2 signatures with SM3 as the digest algorithm.
 */
public abstract class KAESM2Signature extends SignatureSpi {
    /**
     * The current mode, signature or signature verification.
     */
    enum Mode {
        SIGNATURE,
        VERIFY
    }

    /**
     * Message digest algorithm name used for signing. Currently, only SM3 is supported.
     */
    enum DigestName {
        SM3("SM3");

        private final String digestName;

        DigestName(String digestName) {
            this.digestName = digestName;
        }

        public String getDigestValue() {
            return digestName;
        }
    }

    // message digest algorithm name we use
    private final DigestName digestName;

    // private key, if initialized for signing
    private ECPrivateKey privateKey;

    // public key, if initialized for verifying
    private ECPublicKey publicKey;

    // openssl context, save initialization information and updated messages.
    private SM2SignCtxHolder ctxHolder;

    // openssl context copy, reset after signature or verification
    private SM2SignCtxHolder ctxHolderCopy;

    // the current mode
    private Mode mode;

    // initialized or not
    private boolean initialized = false;

    // default value
    private String id = "1234567812345678";

    public KAESM2Signature()  throws NoSuchAlgorithmException{
        this(DigestName.SM3.getDigestValue());
    }

    public KAESM2Signature(String digest)  throws NoSuchAlgorithmException{
        if ("SM3".equals(digest)){
            this.digestName = DigestName.SM3;
        }else {
            throw new NoSuchAlgorithmException("KAESM2Signature not support the " + digest + "digest algorithm");
        }
    }

    /**
     * Initializes this signature object with the specified
     * public key for verification operations.
     *
     * @param publicKey the public key of the identity whose signature is
     *                  going to be verified.
     */
    @Override
    protected void engineInitVerify(PublicKey publicKey) throws InvalidKeyException {
        this.publicKey = (ECPublicKey) KAEECKeyFactory.toECKey(publicKey);
        long keyAddress;
        try {
            int curveLen = (this.publicKey.getParams().getCurve().getField().getFieldSize() + 7) / 8;
            keyAddress = KAESM2Cipher.nativeCreateSM2PublicKey(
                    asUnsignedByteArray(curveLen, this.publicKey.getW().getAffineX()),
                    asUnsignedByteArray(curveLen, this.publicKey.getW().getAffineY()));
        } catch (RuntimeException e) {
            throw new RuntimeException("KAESM2Signature nativeCreateSM2PublicKey failed", e);
        }
        try {
            long verifyCtx = nativeInitSM2Ctx(keyAddress, digestName.getDigestValue(), id, Boolean.FALSE);
            if (verifyCtx == 0){
                throw new InvalidKeyException("engineInitSign verifyCtx is invalid");
            }
            this.ctxHolder = new SM2SignCtxHolder(this, verifyCtx);
        } catch (RuntimeException e) {
            throw new RuntimeException("KAESM2Signature nativeInitSM2Ctx failed", e);
        }finally {
            KAESM2Cipher.nativeFreeKey(keyAddress);
        }
        this.mode = Mode.VERIFY;
        this.initialized = true;
    }

    /**
     * Initializes this signature object with the specified
     * private key for signing operations.
     *
     * @param privateKey the private key of the identity whose signature
     *                   will be generated.
     */
    @Override
    protected void engineInitSign(PrivateKey privateKey) throws InvalidKeyException {
        this.privateKey = (ECPrivateKey) KAEECKeyFactory.toECKey(privateKey);
        long keyAddress;
        try {
            int curveLen = (this.privateKey.getParams().getCurve().getField().getFieldSize() + 7) / 8;
            keyAddress = KAESM2Cipher.nativeCreateSM2PrivateKey(asUnsignedByteArray(curveLen, this.privateKey.getS()), true);
        } catch (RuntimeException e) {
            throw new InvalidKeyException("KAESM2Signature nativeCreateSM2PrivateKey failed", e);
        }
        try {
            long signCtx = nativeInitSM2Ctx(keyAddress, digestName.getDigestValue(), id, Boolean.TRUE);
            if (signCtx == 0){
                throw new InvalidKeyException("engineInitSign signCtx is invalid");
            }
            this.ctxHolder = new SM2SignCtxHolder(this, signCtx);
        } catch (RuntimeException e) {
            throw new RuntimeException("KAESM2Signature nativeInitSM2Ctx failed", e);
        }finally {
            KAESM2Cipher.nativeFreeKey(keyAddress);
        }
        this.mode = Mode.SIGNATURE;
        this.initialized = true;
    }

    // update the signature with the plaintext data. See JCA doc
    @Override
    protected void engineUpdate(byte b) throws SignatureException {
        byte[] msg = new byte[1];
        msg[0] = b;
        engineUpdate(msg, 0, 1);
    }

    // update the signature with the plaintext data. See JCA doc
    @Override
    protected void engineUpdate(byte[] b, int off, int len) throws SignatureException {
        if(!initialized || ctxHolder == null){
            throw new SignatureException("The engine is not initialized");
        }
        byte[] msg = new byte[len];
        System.arraycopy(b, off, msg, 0, len);
        if (ctxHolderCopy == null) {
            ctxHolderCopy = createCtxHolder(this, ctxHolder.ctxAddress);
        }
        try {
            if(this.mode == Mode.SIGNATURE){
                nativeSM2Update(ctxHolderCopy.ctxAddress, msg, len, Boolean.TRUE);
            }else {
                // Mode.VERIFY
                nativeSM2Update(ctxHolderCopy.ctxAddress, msg, len, Boolean.FALSE);
            }
        } catch (RuntimeException e) {
            throw new RuntimeException("KAESM2Signature nativeSM2Update Failed", e);
        }
    }

    // see JCE spec
    @Override
    protected byte[] engineSign() throws SignatureException {
        if(!initialized || ctxHolder == null){
            throw new SignatureException("The engine is not initialized");
        }
        if (ctxHolderCopy == null) {
            ctxHolderCopy = createCtxHolder(this, ctxHolder.ctxAddress);
        }
        byte[] sigBytes;
        try {
            sigBytes = nativeSM2SignFinal(ctxHolderCopy.ctxAddress);
        } catch (SignatureException e){
            throw new RuntimeException("KAESM2Signature nativeSM2SignFinal Failed", e);
        }finally {
            resetCtxHolderCopy();
        }
        return sigBytes;
    }

    // see JCE spec
    @Override
    protected boolean engineVerify(byte[] sigBytes) throws SignatureException {
        if(!initialized || ctxHolder == null){
            throw new SignatureException("The engine is not initialized");
        }
        if (ctxHolderCopy == null) {
            ctxHolderCopy = createCtxHolder(this, ctxHolder.ctxAddress);
        }
        try {
            return nativeSM2VerifyFinal(ctxHolderCopy.ctxAddress, sigBytes, sigBytes.length);
        } catch (SignatureException e){
            throw new RuntimeException("KAESM2Signature nativeSM2VerifyFinal Failed", e);
        }finally {
            resetCtxHolderCopy();
        }
    }

    // set parameter, not supported. See JCA doc
    @Deprecated
    @Override
    protected void engineSetParameter(String param, Object value) throws InvalidParameterException {
        throw new UnsupportedOperationException("setParameter() not supported");
    }

    @Override
    protected void engineSetParameter(AlgorithmParameterSpec params)
            throws InvalidAlgorithmParameterException {
        if (params == null) {
            throw new InvalidAlgorithmParameterException("params is null");
        }

        try {
            Class<?> clazz = params.getClass();
            Field field = clazz.getDeclaredField("id");
            field.setAccessible(true);
            byte[] idValue = (byte[]) field.get(params);
            this.id = new String(idValue, StandardCharsets.UTF_8);
        } catch (IllegalAccessException | NoSuchFieldException e) {
            throw new InvalidAlgorithmParameterException("Failed to get id field from params");
        }
    }

    // get parameter, not supported. See JCA doc
    @Deprecated
    @Override
    protected Object engineGetParameter(String param) throws InvalidParameterException {
        throw new UnsupportedOperationException("getParameter() not supported");
    }

    /**
     * The sm2 sign openssl md_ctx holder , use PhantomReference in case of native memory leaks
     */
    private static class SM2SignCtxHolder extends PhantomReference<KAESM2Signature>
            implements Comparable<SM2SignCtxHolder> {
        private static ReferenceQueue<KAESM2Signature> referenceQueue = new ReferenceQueue<>();
        private static Set<SM2SignCtxHolder> referenceList = new ConcurrentSkipListSet<>();
        private final long ctxAddress;

        private static boolean disableKaeDispose = Boolean.getBoolean("kae.disableKaeDispose");

        SM2SignCtxHolder(KAESM2Signature sm2Cipher, long ctxAddress) {
            super(sm2Cipher, referenceQueue);
            this.ctxAddress = ctxAddress;
            if (!disableKaeDispose) {
                referenceList.add(this);
                drainRefQueueBounded();
            }
        }

        private static void drainRefQueueBounded() {
            while (true) {
                SM2SignCtxHolder next = (SM2SignCtxHolder) referenceQueue.poll();
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
                        nativeFreeSM2Ctx(ctxAddress);
                    }
                } finally {
                    this.clear();
                }
            } else {
                nativeFreeSM2Ctx(ctxAddress);
            }
        }

        @Override
        public int compareTo(SM2SignCtxHolder other) {
            if (this.ctxAddress == other.ctxAddress) {
                return 0;
            } else {
                return (this.ctxAddress < other.ctxAddress) ? -1 : 1;
            }
        }
    }

    // reset the ctx holder
    private void resetCtxHolderCopy() {
        if (ctxHolderCopy != null) {
            ctxHolderCopy.dispose(true);
            ctxHolderCopy = null;
        }
    }

    private SM2SignCtxHolder createCtxHolder(KAESM2Signature kaesm2Signature, long ctxAddress) {
        long addr;
        try {
            addr = nativeClone(ctxAddress);
        } catch (RuntimeException e) {
            throw new RuntimeException("SM2SignCtxHolder nativeClone failed", e);
        }
        if (addr == 0) {
            throw new RuntimeException("SM2SignCtxHolder nativeClone EVP_MD_CTX failed");
        }
        return new SM2SignCtxHolder(kaesm2Signature, addr);
    }

    // clone the sign ctx
    protected static native long nativeClone(long ctxAddress);

    // free the sign ctx
    protected static native void nativeFreeSM2Ctx(long ctxAddress);

    // init openssl sm2 signature context
    protected static native long nativeInitSM2Ctx(long keyAddress, String digestName, String id, boolean isSign);

    // update openssl sm2 signature text
    protected static native void nativeSM2Update(long ctxAddress, byte[] msg, int msgLen, boolean isSign);

    // sm2 signature do final
    protected static native byte[] nativeSM2SignFinal(long ctxAddress) throws SignatureException;

    // sm2 verification do final
    protected static native boolean nativeSM2VerifyFinal(long ctxAddress, byte[] sigBytes, int sigLen) throws SignatureException;

    static public class SM3withSM2
            extends KAESM2Signature {
        public SM3withSM2() throws NoSuchAlgorithmException {
            super(DigestName.SM3.getDigestValue());
        }
    }
}
