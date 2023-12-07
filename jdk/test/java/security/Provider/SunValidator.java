/*
 * Copyright (c) 2022, Huawei Technologies Co., Ltd. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Huawei designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Huawei in the LICENSE file that accompanied this code.
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
 * Please visit https://gitee.com/openeuler/bishengjdk-8 if you need additional
 * information or have any questions.
 */

/*
 * @test
 * @bug 7092821
 * @library ../testlibrary
 * @summary make sure that Sun providers do not miss any algorithms after
 *   modifying the frameworks underneath
 * @author Henry Yang
 */

/*
 *- @TestCaseID:Provider/SunValidator.java
 *- @TestCaseName:Provider/SunValidator.java
 *- @TestCaseType:Function test
 *- @RequirementID:AR.SR.IREQ02758058.001.001
 *- @RequirementName: java.security.Provider.getService() is synchronized and became scalability bottleneck
 *- @Condition:JDK8u302 and later
 *- @Brief:Check whether the service provided by the corresponding provider after changing the underlying architecture is different from the original one (subject to openJDK8u302).
 *   -#step:Compare whether the service provided by openJDK8u302 SunProvider is consistent with the modified SunProvider with this feature.
 *- @Expect:Normal Running.
 *- @Priority:Level 1
 */

import sun.security.provider.NativePRNG;
import sun.security.provider.Sun;

import java.lang.reflect.Method;
import java.security.Provider;

/**
 * validator for Sun provider, make sure we do not miss any algorithm
 * after the modification.
 *
 * @author Henry Yang
 * @since 2022-05-05
 */
public class SunValidator extends BaseProviderValidator {
    public static void main(String[] args) throws Exception {
        SunValidator validator = new SunValidator();
        validator.validate();
    }

    @Override
    Provider getDefaultProvider() {
        return new Sun();
    }

    @Override
    public boolean validate() throws Exception {
        Method nativeAvailableMethod = NativePRNG.class.getDeclaredMethod("isAvailable");
        nativeAvailableMethod.setAccessible(true);
        boolean nativeAvailable = (Boolean) nativeAvailableMethod.invoke(null);
        if (nativeAvailable) {
            checkService("SecureRandom.NativePRNG");
        }

        checkService("SecureRandom.SHA1PRNG");

        /*
         * Signature engines
         */
        checkService("Signature.SHA1withDSA");
        checkService("Signature.NONEwithDSA");
        checkAlias("Alg.Alias.Signature.RawDSA", "NONEwithDSA");
        checkService("Signature.SHA224withDSA");
        checkService("Signature.SHA256withDSA");

        String dsaKeyClasses = "java.security.interfaces.DSAPublicKey" + "|java.security.interfaces.DSAPrivateKey";
        checkAttribute("Signature.SHA1withDSA SupportedKeyClasses", dsaKeyClasses);
        checkAttribute("Signature.NONEwithDSA SupportedKeyClasses", dsaKeyClasses);
        checkAttribute("Signature.SHA224withDSA SupportedKeyClasses", dsaKeyClasses);
        checkAttribute("Signature.SHA256withDSA SupportedKeyClasses", dsaKeyClasses);

        checkAlias("Alg.Alias.Signature.DSA", "SHA1withDSA");
        checkAlias("Alg.Alias.Signature.DSS", "SHA1withDSA");
        checkAlias("Alg.Alias.Signature.SHA/DSA", "SHA1withDSA");
        checkAlias("Alg.Alias.Signature.SHA-1/DSA", "SHA1withDSA");
        checkAlias("Alg.Alias.Signature.SHA1/DSA", "SHA1withDSA");
        checkAlias("Alg.Alias.Signature.SHAwithDSA", "SHA1withDSA");
        checkAlias("Alg.Alias.Signature.DSAWithSHA1", "SHA1withDSA");
        checkAlias("Alg.Alias.Signature.OID.1.2.840.10040.4.3", "SHA1withDSA");
        checkAlias("Alg.Alias.Signature.1.2.840.10040.4.3", "SHA1withDSA");
        checkAlias("Alg.Alias.Signature.1.3.14.3.2.13", "SHA1withDSA");
        checkAlias("Alg.Alias.Signature.1.3.14.3.2.27", "SHA1withDSA");
        checkAlias("Alg.Alias.Signature.OID.2.16.840.1.101.3.4.3.1", "SHA224withDSA");
        checkAlias("Alg.Alias.Signature.2.16.840.1.101.3.4.3.1", "SHA224withDSA");
        checkAlias("Alg.Alias.Signature.OID.2.16.840.1.101.3.4.3.2", "SHA256withDSA");
        checkAlias("Alg.Alias.Signature.2.16.840.1.101.3.4.3.2", "SHA256withDSA");
        System.out.println("Signature engines check passed");

        /*
         *  Key Pair Generator engines
         */
        checkService("KeyPairGenerator.DSA");
        checkAlias("Alg.Alias.KeyPairGenerator.OID.1.2.840.10040.4.1", "DSA");
        checkAlias("Alg.Alias.KeyPairGenerator.1.2.840.10040.4.1", "DSA");
        checkAlias("Alg.Alias.KeyPairGenerator.1.3.14.3.2.12", "DSA");
        System.out.println("Key Pair Generator engines check passed");

        /*
         * Digest engines
         */
        checkService("MessageDigest.MD2");
        checkService("MessageDigest.MD5");
        checkService("MessageDigest.SHA");

        checkAlias("Alg.Alias.MessageDigest.SHA-1", "SHA");
        checkAlias("Alg.Alias.MessageDigest.SHA1", "SHA");
        checkAlias("Alg.Alias.MessageDigest.1.3.14.3.2.26", "SHA");
        checkAlias("Alg.Alias.MessageDigest.OID.1.3.14.3.2.26", "SHA");

        checkService("MessageDigest.SHA-224");
        checkAlias("Alg.Alias.MessageDigest.2.16.840.1.101.3.4.2.4", "SHA-224");
        checkAlias("Alg.Alias.MessageDigest.OID.2.16.840.1.101.3.4.2.4", "SHA-224");

        checkService("MessageDigest.SHA-256");
        checkAlias("Alg.Alias.MessageDigest.2.16.840.1.101.3.4.2.1", "SHA-256");
        checkAlias("Alg.Alias.MessageDigest.OID.2.16.840.1.101.3.4.2.1", "SHA-256");
        checkService("MessageDigest.SHA-384");
        checkAlias("Alg.Alias.MessageDigest.2.16.840.1.101.3.4.2.2", "SHA-384");
        checkAlias("Alg.Alias.MessageDigest.OID.2.16.840.1.101.3.4.2.2", "SHA-384");
        checkService("MessageDigest.SHA-512");
        checkAlias("Alg.Alias.MessageDigest.2.16.840.1.101.3.4.2.3", "SHA-512");
        checkAlias("Alg.Alias.MessageDigest.OID.2.16.840.1.101.3.4.2.3", "SHA-512");
        checkService("MessageDigest.SHA-512/224");
        checkAlias("Alg.Alias.MessageDigest.2.16.840.1.101.3.4.2.5", "SHA-512/224");
        checkAlias("Alg.Alias.MessageDigest.OID.2.16.840.1.101.3.4.2.5", "SHA-512/224");
        checkService("MessageDigest.SHA-512/256");
        checkAlias("Alg.Alias.MessageDigest.2.16.840.1.101.3.4.2.6", "SHA-512/256");
        checkAlias("Alg.Alias.MessageDigest.OID.2.16.840.1.101.3.4.2.6", "SHA-512/256");
        System.out.println("Digest engines check passed");

        /*
         * Algorithm Parameter Generator engines
         */
        checkService("AlgorithmParameterGenerator.DSA");
        System.out.println("Algorithm Parameter Generator engines check passed");

        /*
         * Algorithm Parameter engines
         */
        checkService("AlgorithmParameters.DSA");
        checkAlias("Alg.Alias.AlgorithmParameters.OID.1.2.840.10040.4.1", "DSA");
        checkAlias("Alg.Alias.AlgorithmParameters.1.2.840.10040.4.1", "DSA");
        checkAlias("Alg.Alias.AlgorithmParameters.1.3.14.3.2.12", "DSA");
        System.out.println("Algorithm Parameter engines check passed");

        /*
         * Key factories
         */
        checkService("KeyFactory.DSA");
        checkAlias("Alg.Alias.KeyFactory.OID.1.2.840.10040.4.1", "DSA");
        checkAlias("Alg.Alias.KeyFactory.1.2.840.10040.4.1", "DSA");
        checkAlias("Alg.Alias.KeyFactory.1.3.14.3.2.12", "DSA");
        System.out.println("Key factories check passed");

        /*
         * Certificates
         */
        checkService("CertificateFactory.X.509");
        checkAlias("Alg.Alias.CertificateFactory.X509", "X.509");
        System.out.println("Certificates check passed");

        /*
         * KeyStore
         */
        checkService("KeyStore.JKS");
        checkService("KeyStore.CaseExactJKS");
        checkService("KeyStore.DKS");
        System.out.println("KeyStore check passed");

        /*
         * Policy
         */
        checkService("Policy.JavaPolicy");
        System.out.println("Policy check passed");

        /*
         * Configuration
         */
        checkService("Configuration.JavaLoginConfig");
        System.out.println("Configuration check passed");

        /*
         * CertPathBuilder
         */
        checkService("CertPathBuilder.PKIX");
        checkAttribute("CertPathBuilder.PKIX ValidationAlgorithm", "RFC5280");
        System.out.println("CertPathBuilder check passed");

        /*
         * CertPathValidator
         */
        checkService("CertPathValidator.PKIX");
        checkAttribute("CertPathValidator.PKIX ValidationAlgorithm", "RFC5280");
        System.out.println("CertPathValidator check passed");

        /*
         * CertStores
         */
        checkService("CertStore.LDAP");
        checkAttribute("CertStore.LDAP LDAPSchema", "RFC2587");
        checkService("CertStore.Collection");
        checkService("CertStore.com.sun.security.IndexedCollection");
        System.out.println("CertStores check passed");

        /*
         * KeySize
         */
        checkAttribute("Signature.NONEwithDSA KeySize", "1024");
        checkAttribute("Signature.SHA1withDSA KeySize", "1024");
        checkAttribute("Signature.SHA224withDSA KeySize", "2048");
        checkAttribute("Signature.SHA256withDSA KeySize", "2048");

        checkAttribute("KeyPairGenerator.DSA KeySize", "2048");
        checkAttribute("AlgorithmParameterGenerator.DSA KeySize", "2048");
        System.out.println("KeySize attribute check passed");

        /*
         * Implementation type: software or hardware
         */
        checkAttribute("Signature.SHA1withDSA ImplementedIn", "Software");
        checkAttribute("KeyPairGenerator.DSA ImplementedIn", "Software");
        checkAttribute("MessageDigest.MD5 ImplementedIn", "Software");
        checkAttribute("MessageDigest.SHA ImplementedIn", "Software");
        checkAttribute("AlgorithmParameterGenerator.DSA ImplementedIn", "Software");
        checkAttribute("AlgorithmParameters.DSA ImplementedIn", "Software");
        checkAttribute("KeyFactory.DSA ImplementedIn", "Software");
        checkAttribute("SecureRandom.SHA1PRNG ImplementedIn", "Software");
        checkAttribute("CertificateFactory.X.509 ImplementedIn", "Software");
        checkAttribute("KeyStore.JKS ImplementedIn", "Software");
        checkAttribute("CertPathValidator.PKIX ImplementedIn", "Software");
        checkAttribute("CertPathBuilder.PKIX ImplementedIn", "Software");
        checkAttribute("CertStore.LDAP ImplementedIn", "Software");
        checkAttribute("CertStore.Collection ImplementedIn", "Software");
        checkAttribute("CertStore.com.sun.security.IndexedCollection ImplementedIn", "Software");
        System.out.println("Implementation type attribute check passed");
        return true;
    }
}
