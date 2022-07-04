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
 *- @TestCaseID:Provider/SunRsaSignValidator.java
 *- @TestCaseName:Provider/SunRsaSignValidator.java
 *- @TestCaseType:Function test
 *- @RequirementID:AR.SR.IREQ02758058.001.001
 *- @RequirementName: java.security.Provider.getService() is synchronized and became scalability bottleneck
 *- @Condition:JDK8u302及以后
 *- @Brief:测试相应provider更改底层架构以后所提供的service是否与原先有差异（以openJDK8u302为准）
 *   -#step:比较openJDK8u302 SunRsaSignProvider与此特性修改后的SunRsaSignProvider所提供的service是否一致
 *- @Expect:正常运行
 *- @Priority:Level 1
 */

import sun.security.rsa.SunRsaSign;

import java.security.Provider;

/**
 * validator for SunRsaSign provider, make sure we do not miss any algorithm
 * after the modification.
 *
 * @author Henry Yang
 * @since 2022-05-05
 */
public class SunRsaSignValidator extends BaseProviderValidator {
    public static void main(String[] args) throws Exception {
        SunRsaSignValidator validator = new SunRsaSignValidator();
        validator.validate();
    }

    @Override
    Provider getDefaultProvider() {
        return new SunRsaSign();
    }

    @Override
    boolean validate() throws Exception {
        // main algorithms
        checkService("KeyFactory.RSA");
        checkService("KeyPairGenerator.RSA");
        checkService("Signature.MD2withRSA");
        checkService("Signature.MD5withRSA");
        checkService("Signature.SHA1withRSA");
        checkService("Signature.SHA224withRSA");
        checkService("Signature.SHA256withRSA");
        checkService("Signature.SHA384withRSA");
        checkService("Signature.SHA512withRSA");
        checkService("Signature.SHA512/224withRSA");
        checkService("Signature.SHA512/256withRSA");

        checkService("KeyFactory.RSASSA-PSS");
        checkService("KeyPairGenerator.RSASSA-PSS");
        checkService("Signature.RSASSA-PSS");
        checkService("AlgorithmParameters.RSASSA-PSS");

        System.out.println("service check passed");

        // attributes for supported key classes
        String rsaKeyClasses = "java.security.interfaces.RSAPublicKey" + "|java.security.interfaces.RSAPrivateKey";
        checkAttribute("Signature.MD2withRSA SupportedKeyClasses", rsaKeyClasses);
        checkAttribute("Signature.MD5withRSA SupportedKeyClasses", rsaKeyClasses);
        checkAttribute("Signature.SHA1withRSA SupportedKeyClasses", rsaKeyClasses);
        checkAttribute("Signature.SHA224withRSA SupportedKeyClasses", rsaKeyClasses);
        checkAttribute("Signature.SHA256withRSA SupportedKeyClasses", rsaKeyClasses);
        checkAttribute("Signature.SHA384withRSA SupportedKeyClasses", rsaKeyClasses);
        checkAttribute("Signature.SHA512withRSA SupportedKeyClasses", rsaKeyClasses);
        checkAttribute("Signature.SHA512/224withRSA SupportedKeyClasses", rsaKeyClasses);
        checkAttribute("Signature.SHA512/256withRSA SupportedKeyClasses", rsaKeyClasses);
        checkAttribute("Signature.RSASSA-PSS SupportedKeyClasses", rsaKeyClasses);

        System.out.println("attribute check passed");

        // aliases
        checkAlias("Alg.Alias.KeyFactory.1.2.840.113549.1.1", "RSA");
        checkAlias("Alg.Alias.KeyFactory.OID.1.2.840.113549.1.1", "RSA");

        checkAlias("Alg.Alias.KeyPairGenerator.1.2.840.113549.1.1", "RSA");
        checkAlias("Alg.Alias.KeyPairGenerator.OID.1.2.840.113549.1.1", "RSA");

        checkAlias("Alg.Alias.Signature.1.2.840.113549.1.1.2", "MD2withRSA");
        checkAlias("Alg.Alias.Signature.OID.1.2.840.113549.1.1.2", "MD2withRSA");

        checkAlias("Alg.Alias.Signature.1.2.840.113549.1.1.4", "MD5withRSA");
        checkAlias("Alg.Alias.Signature.OID.1.2.840.113549.1.1.4", "MD5withRSA");

        checkAlias("Alg.Alias.Signature.1.2.840.113549.1.1.5", "SHA1withRSA");
        checkAlias("Alg.Alias.Signature.OID.1.2.840.113549.1.1.5", "SHA1withRSA");
        checkAlias("Alg.Alias.Signature.1.3.14.3.2.29", "SHA1withRSA");

        checkAlias("Alg.Alias.Signature.1.2.840.113549.1.1.14", "SHA224withRSA");
        checkAlias("Alg.Alias.Signature.OID.1.2.840.113549.1.1.14", "SHA224withRSA");

        checkAlias("Alg.Alias.Signature.1.2.840.113549.1.1.11", "SHA256withRSA");
        checkAlias("Alg.Alias.Signature.OID.1.2.840.113549.1.1.11", "SHA256withRSA");

        checkAlias("Alg.Alias.Signature.1.2.840.113549.1.1.12", "SHA384withRSA");
        checkAlias("Alg.Alias.Signature.OID.1.2.840.113549.1.1.12", "SHA384withRSA");

        checkAlias("Alg.Alias.Signature.1.2.840.113549.1.1.13", "SHA512withRSA");
        checkAlias("Alg.Alias.Signature.OID.1.2.840.113549.1.1.13", "SHA512withRSA");
        checkAlias("Alg.Alias.Signature.1.2.840.113549.1.1.15", "SHA512/224withRSA");
        checkAlias("Alg.Alias.Signature.OID.1.2.840.113549.1.1.15", "SHA512/224withRSA");
        checkAlias("Alg.Alias.Signature.1.2.840.113549.1.1.16", "SHA512/256withRSA");
        checkAlias("Alg.Alias.Signature.OID.1.2.840.113549.1.1.16", "SHA512/256withRSA");

        checkAlias("Alg.Alias.KeyFactory.1.2.840.113549.1.1.10", "RSASSA-PSS");
        checkAlias("Alg.Alias.KeyFactory.OID.1.2.840.113549.1.1.10", "RSASSA-PSS");

        checkAlias("Alg.Alias.KeyPairGenerator.1.2.840.113549.1.1.10", "RSASSA-PSS");
        checkAlias("Alg.Alias.KeyPairGenerator.OID.1.2.840.113549.1.1.10", "RSASSA-PSS");

        checkAlias("Alg.Alias.Signature.1.2.840.113549.1.1.10", "RSASSA-PSS");
        checkAlias("Alg.Alias.Signature.OID.1.2.840.113549.1.1.10", "RSASSA-PSS");

        checkAlias("Alg.Alias.AlgorithmParameters.1.2.840.113549.1.1.10", "RSASSA-PSS");
        checkAlias("Alg.Alias.AlgorithmParameters.OID.1.2.840.113549.1.1.10", "RSASSA-PSS");

        System.out.println("check alias passed");
        return true;
    }
}
