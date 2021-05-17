/*
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


import java.io.Serializable;
import java.math.BigInteger;
import java.security.*;
import java.util.Arrays;
import java.util.Date;
import javax.crypto.KeyAgreement;
import javax.crypto.spec.*;
import org.openeuler.security.openssl.KAEProvider;

/**
 * This class implements the Diffie-Hellman key exchange algorithm.
 * D-H means combining your private key with your partners public key to
 * generate a number. The peer does the same with its private key and our
 * public key. Through the magic of Diffie-Hellman we both come up with the
 * same number. This number is secret (discounting MITM attacks) and hence
 * called the shared secret. It has the same length as the modulus, e.g. 512
 * or 1024 bit. Man-in-the-middle attacks are typically countered by an
 * independent authentication step using certificates (RSA, DSA, etc.).
 *
 * The thing to note is that the shared secret is constant for two partners
 * with constant private keys. This is often not what we want, which is why
 * it is generally a good idea to create a new private key for each session.
 * Generating a private key involves one modular exponentiation assuming
 * suitable D-H parameters are available.
 *
 * General usage of this class (TLS DHE case):
 *  . if we are server, call DHCrypt(keyLength,random). This generates
 *    an ephemeral keypair of the request length.
 *  . if we are client, call DHCrypt(modulus, base, random). This
 *    generates an ephemeral keypair using the parameters specified by
 *    the server.
 *  . send parameters and public value to remote peer
 *  . receive peers ephemeral public key
 *  . call getAgreedSecret() to calculate the shared secret
 *
 * In TLS the server chooses the parameter values itself, the client must use
 * those sent to it by the server.
 *
 * The use of ephemeral keys as described above also achieves what is called
 * "forward secrecy". This means that even if the authentication keys are
 * broken at a later date, the shared secret remains secure. The session is
 * compromised only if the authentication keys are already broken at the
 * time the key exchange takes place and an active MITM attack is used.
 * This is in contrast to straightforward encrypting RSA key exchanges.
 *
 */


/**
 * @test
 * @summary Basic test for DH
 * @run main DHTest
 */

final class DHTest implements Serializable {
    private  static int bitLength = 8192;
    private static BigInteger g512;
    private static BigInteger p512;
    Throwable t = null;

    private static volatile Provider sunJceProvider;
    private static volatile Provider kaeProvider;
    Date d = new Date();

    public static void main(String[] args) throws Exception {
        Security.addProvider(new KAEProvider());
        sunJceProvider = Security.getProvider("SunJCE");
        kaeProvider = Security.getProvider("KAEProvider");

        g512 = new BigInteger("30270326776012916323988175351831539351616124181011347910931933302640902603480679235129557617566480716138395926949700593986872757726720164601940036524221141391913433558162442022339559255078339658108149162251643458301671465579040759659507434340437396584664407572026953757806341363255195983514333141770938654900099033797866272818739547343977583089845850158637618703095710047154252655157633638171416516716598940884520592858762209135010804267830977334033327483815694794951984230264309784679409488441905236794443014066406150649287037909246107758452315504212879842042858577191624250834553614056794526338841821045329189780334");

        p512 = new BigInteger("27672987386729926592037876826877634387173876890702920770064392919138769821035856568775311919542560094764667151024449425954917954337048895981297730855891532066350935045229294626339548842381843985759061682551900379979643117695834175891578650111093016914264824311693147701566019122696621248493126219217339690346346921463135605151471303957324058301097079967414639146647429422884520134312590056632178576758580657240245655739869017244657144448267757255018625514803292549109401806336918448001843022629625467069714240279603204909633404992842479161100500474744098408277938070656334892106100534117209709263785505019003765693651");

        DHTest.bitLength = 0;

        DHParameterSpec dhParams = new DHParameterSpec(p512, g512);
        KeyPairGenerator SunJCEkeyGen = KeyPairGenerator.getInstance("DH", sunJceProvider);
        KeyPairGenerator KAEkeyGen = KeyPairGenerator.getInstance("DH", kaeProvider);
        SunJCEkeyGen.initialize(dhParams, new SecureRandom());
        KAEkeyGen.initialize(dhParams, new SecureRandom());
        KeyAgreement aKeyAgree = KeyAgreement.getInstance("DH", sunJceProvider);
        KeyPair aPair = SunJCEkeyGen.generateKeyPair();
        KeyAgreement bKeyAgree = KeyAgreement.getInstance("DH", kaeProvider);
        KeyPair bPair = KAEkeyGen.generateKeyPair();

        aKeyAgree.init(aPair.getPrivate());
        bKeyAgree.init(bPair.getPrivate());

        aKeyAgree.doPhase(bPair.getPublic(), true);
        bKeyAgree.doPhase(aPair.getPublic(), true);

        MessageDigest hash = MessageDigest.getInstance("MD5");
        byte[] b1 = hash.digest(aKeyAgree.generateSecret());
        byte[] b2 = hash.digest(bKeyAgree.generateSecret());
        if(Arrays.equals(b1, b2)){
            System.out.println("SUCCESS!");
        }else{
            System.out.println("Failed!");
            throw new RuntimeException("Not Equal DH keyagreement ouput from SunJCE and KAE Provider!");
        }

    }

}
