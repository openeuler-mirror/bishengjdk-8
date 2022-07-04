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
 *- @TestCaseID:Provider/SunJSSEValidator.java
 *- @TestCaseName:Provider/SunJSSEValidator.java
 *- @TestCaseType:Function test
 *- @RequirementID:AR.SR.IREQ02758058.001.001
 *- @RequirementName: java.security.Provider.getService() is synchronized and became scalability bottleneck
 *- @Condition:JDK8u302及以后
 *- @Brief:测试相应provider更改底层架构以后所提供的service是否与原先有差异（以openJDK8u302为准）
 *   -#step:比较openJDK8u302 SunJSSEProvider与此特性修改后的SunJSSEProvider所提供的service是否一致
 *- @Expect:正常运行
 *- @Priority:Level 1
 */

import java.security.Provider;
import java.util.Locale;

/**
 * validator for SunJSSE provider, make sure we do not miss any algorithm
 * after the modification.
 *
 * @author Henry Yang
 * @since 2022-05-05
 */
public class SunJSSEValidator extends BaseProviderValidator {
    private boolean fips = false;

    public static void main(String[] args) throws Exception {
        SunJSSEValidator validator = new SunJSSEValidator();
        if (args != null && args.length > 0) {
            String fipsStr = args[0].toLowerCase(Locale.ENGLISH);
            if (!"true".equals(fipsStr) && !"false".equals(fipsStr)) {
                throw new RuntimeException("Fips mode argument should be a boolean value");
            }
            validator.setFips(Boolean.parseBoolean(fipsStr));
        }
        validator.validate();
    }

    public void setFips(boolean isFips) {
        this.fips = isFips;
    }

    @Override
    Provider getDefaultProvider() {
        return new com.sun.net.ssl.internal.ssl.Provider();
    }

    @Override
    boolean validate() throws Exception {
        if (fips == false) {
            checkService("KeyFactory.RSA");
            checkAlias("Alg.Alias.KeyFactory.1.2.840.113549.1.1", "RSA");
            checkAlias("Alg.Alias.KeyFactory.OID.1.2.840.113549.1.1", "RSA");

            checkService("KeyPairGenerator.RSA");
            checkAlias("Alg.Alias.KeyPairGenerator.1.2.840.113549.1.1", "RSA");
            checkAlias("Alg.Alias.KeyPairGenerator.OID.1.2.840.113549.1.1", "RSA");

            checkService("Signature.MD2withRSA");
            checkAlias("Alg.Alias.Signature.1.2.840.113549.1.1.2", "MD2withRSA");
            checkAlias("Alg.Alias.Signature.OID.1.2.840.113549.1.1.2", "MD2withRSA");

            checkService("Signature.MD5withRSA");
            checkAlias("Alg.Alias.Signature.1.2.840.113549.1.1.4", "MD5withRSA");
            checkAlias("Alg.Alias.Signature.OID.1.2.840.113549.1.1.4", "MD5withRSA");

            checkService("Signature.SHA1withRSA");
            checkAlias("Alg.Alias.Signature.1.2.840.113549.1.1.5", "SHA1withRSA");
            checkAlias("Alg.Alias.Signature.OID.1.2.840.113549.1.1.5", "SHA1withRSA");
            checkAlias("Alg.Alias.Signature.1.3.14.3.2.29", "SHA1withRSA");
            checkAlias("Alg.Alias.Signature.OID.1.3.14.3.2.29", "SHA1withRSA");
        }
        checkService("Signature.MD5andSHA1withRSA");

        checkService("KeyManagerFactory.SunX509");
        checkService("KeyManagerFactory.NewSunX509");
        checkAlias("Alg.Alias.KeyManagerFactory.PKIX", "NewSunX509");

        checkService("TrustManagerFactory.SunX509");
        checkService("TrustManagerFactory.PKIX");
        checkAlias("Alg.Alias.TrustManagerFactory.SunPKIX", "PKIX");
        checkAlias("Alg.Alias.TrustManagerFactory.X509", "PKIX");
        checkAlias("Alg.Alias.TrustManagerFactory.X.509", "PKIX");

        checkService("SSLContext.TLSv1");
        checkService("SSLContext.TLSv1.1");
        checkService("SSLContext.TLSv1.2");
        checkService("SSLContext.TLSv1.3");
        checkService("SSLContext.TLS");
        if (fips == false) {
            checkAlias("Alg.Alias.SSLContext.SSL", "TLS");
            checkAlias("Alg.Alias.SSLContext.SSLv3", "TLSv1");
        }

        checkService("SSLContext.Default");

        /*
         * KeyStore
         */
        checkService("KeyStore.PKCS12");
        System.out.println("SunJSSE check passed");
        return true;
    }
}
