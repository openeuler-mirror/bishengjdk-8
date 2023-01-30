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
 * @bug 7092821
 * @library ../testlibrary
 * @summary make sure that Sun providers do not miss any algorithms after
 *   modifying the frameworks underneath
 * @author Henry Yang
 */

import java.security.Provider;
import java.security.Provider.Service;

/**
 * Base class for a provider validator
 *
 * @author Henry Yang
 * @since 2022-05-05
 */
public abstract class BaseProviderValidator {
    String providerName;
    Provider provider;

    public BaseProviderValidator() {
        provider = getDefaultProvider();
        providerName = provider.getName();
    }

    abstract Provider getDefaultProvider();

    abstract boolean validate() throws Exception;

    Service getService(String type, String algo) {
        return ProviderValidationUtil.getService(provider, type, algo);
    }

    boolean checkService(String serviceName) {
        String[] typeAndAlg = ProviderValidationUtil.getTypeAndAlgorithm(serviceName);
        if(typeAndAlg == null || typeAndAlg.length < 2){
            throw new RuntimeException("service name is not in a right formation");
        }
        return ProviderValidationUtil.checkService(provider, typeAndAlg[0], typeAndAlg[1]);
    }

    boolean checkAlias(String aliasFullName, String serviceShortName) {
        return ProviderValidationUtil.checkAlias(provider, aliasFullName, serviceShortName);
    }

    boolean checkAttribute(String attrName, String attrValue) {
        String[] nameAndAttr = attrName.split("\\s+");
        return ProviderValidationUtil.checkAttribute(provider, nameAndAttr[0], nameAndAttr[1], attrValue);
    }
}
