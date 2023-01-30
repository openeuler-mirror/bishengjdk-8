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

import static java.util.Locale.ENGLISH;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.security.Provider;
import java.security.Provider.Service;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * utils for provider validator
 *
 * @author Henry Yang
 * @since 2022-05-05
 */
public class ProviderValidationUtil {
    private static final String ALIAS_PREFIX_LOWER = "alg.alias.";
    private static final int ALIAS_LENGTH = ALIAS_PREFIX_LOWER.length();

    /**
     * get a service from a provider for a specific algorithm
     *
     * @param provider the provider to get a service
     * @param type algorithm type
     * @param algo algorithm name
     * @return the service of the specific algorithm
     */
    public static Service getService(Provider provider, String type, String algo) {
        Service service = provider.getService(type, algo);
        if (service == null) {
            throw new ServiceNotFoundException(provider.getName(), getServiceName(type, algo));
        }
        return service;
    }

    /**
     * checks if the provider offers services for a specific algorithm
     *
     * @param provider the provider to check
     * @param type algorithm type
     * @param algo algorithm name
     * @return true if passed this check
     */
    public static boolean checkService(Provider provider, String type, String algo) {
        Service service = getService(provider, type, algo);
        String className = service.getClassName();
        if (className == null) {
            throw new ServiceNotFoundException(provider.getName(), getServiceName(type, algo));
        }
        try {
            Class.forName(className);
        } catch (ClassNotFoundException e) {
            throw new ServiceNotFoundException(provider.getName(), getServiceName(type, algo));
        }
        return true;
    }

    private static List<String> getAlias(Service service) {
        try {
            Method method = Service.class.getDeclaredMethod("getAliases");
            method.setAccessible(true);
            List<String> aliases = (List) method.invoke(service, null);
            return aliases;
        } catch (NoSuchMethodException | InvocationTargetException | IllegalAccessException e) {
            e.printStackTrace();
        }
        return Collections.<String>emptyList();
    }

    /**
     * check if the provider associates the alias name to the service
     *
     * @param provider the provider to check
     * @param aliasFullName alias
     * @param serviceShortName service name for short
     * @return true if passed this check
     */
    public static boolean checkAlias(Provider provider, String aliasFullName, String serviceShortName) {
        if (aliasFullName.toLowerCase(ENGLISH).startsWith(ALIAS_PREFIX_LOWER)) {
            // for example, in provider defination put("Alg.Alias.MessageDigest.SHA", "SHA-1");
            // Alg.Alias.MessageDigest.SHA for the aliasFullNanme and SHA-1 for serviceShortName
            // the aliasKey is MessageDigest.SHA
            String aliasKey = aliasFullName.substring(ALIAS_LENGTH);
            String[] typeAndAlg = getTypeAndAlgorithm(aliasKey);
            if (typeAndAlg == null || typeAndAlg.length < 2) {
                throw new NameMalFormatException("alias name and type cannot be null");
            }
            String type = typeAndAlg[0];
            String aliasAlg = typeAndAlg[1].intern();
            Service aliasService = provider.getService(type, aliasAlg);
            if (aliasService == null) {
                throw new ServiceNotFoundException(provider.getName(), getServiceName(type, aliasAlg));
            }
            Service service = provider.getService(type, serviceShortName);
            if (service == null) {
                throw new ServiceNotFoundException(provider.getName(), getServiceName(type, serviceShortName));
            }
            if (service != aliasService || !checkAliasInService(service, aliasAlg)) {
                throw new AliasNotMatchedException(
                        getServiceName(type, aliasAlg), getServiceName(type, serviceShortName));
            }
        } else {
            throw new NameMalFormatException("Alias name is not in a proper format");
        }
        return true;
    }

    private static boolean checkAliasInService(Service service, String... aliasArray) {
        List<String> aliases = getAlias(service);
        Set<String> aliasesSet = new HashSet<>();
        aliasesSet.addAll(aliases);
        for (String aliasName : aliasArray) {
            if (!aliasesSet.contains(aliasName)) {
                return false;
            }
        }
        return true;
    }

    /**
     * check if the service has a specific attribute with the correct value in the provider
     *
     * @param provider the provider to check
     * @param serviceName service name
     * @param attrName attribute name
     * @param attrValue attribute value
     * @return true if passed this check
     */
    public static boolean checkAttribute(Provider provider, String serviceName, String attrName, String attrValue) {
        String[] typeAndAlg = getTypeAndAlgorithm(serviceName);
        if (typeAndAlg == null || typeAndAlg.length < 2) {
            throw new NameMalFormatException("service name is not in a right formation");
        }
        Service service = getService(provider, typeAndAlg[0], typeAndAlg[1]);
        return checkAttribute(service, attrName, attrValue);
    }

    private static boolean checkAttribute(Service service, String attrName, String attrValue) {
        if (!attrValue.equals(service.getAttribute(attrName))) {
            throw new AttributeNotFoundException(service.getType(), service.getAlgorithm(), attrName, attrValue);
        }
        return true;
    }

    private static String getServiceName(String type, String algo) {
        return type + "." + algo;
    }

    /**
     * seperate algorithm key with type and name
     *
     * @param key algorithm full name
     * @return string array with algorithm type and name
     */
    public static String[] getTypeAndAlgorithm(String key) {
        int index = key.indexOf('.');
        if (index < 1) {
            return new String[0];
        }
        String type = key.substring(0, index);
        String alg = key.substring(index + 1);
        return new String[] {type, alg};
    }

    /**
     * throws this exception if we cannot find the service in the provider
     *
     * @author Henry Yang
     * @since 2022-05-05
     */
    public static class ServiceNotFoundException extends RuntimeException {
        public ServiceNotFoundException(String provider, String serviceName) {
            this("faild to find " + serviceName + " in " + provider + " provider");
        }

        public ServiceNotFoundException(String message) {
            super(message);
        }
    }

    /**
     * throws this exception if we cannot find the attribute in the service
     * or the attribute value is not correct
     *
     * @author Henry Yang
     * @since 2022-05-05
     */
    public static class AttributeNotFoundException extends RuntimeException {
        public AttributeNotFoundException(String type, String algo, String attrName, String attrValue) {
            this(
                    "faild "
                            + type
                            + "."
                            + algo
                            + " '"
                            + attrName
                            + "' attribute check, "
                            + "the correct value should be '"
                            + attrValue
                            + "'");
        }

        public AttributeNotFoundException(String message) {
            super(message);
        }
    }

    /**
     * throws this exception if we cannot find the alias name in the provider
     *
     * @author Henry Yang
     * @since 2022-05-05
     */
    public static class AliasNotMatchedException extends RuntimeException {
        public AliasNotMatchedException(String aliasName, String serviceName) {
            this("faild to find alias name " + aliasName + " in " + serviceName);
        }

        public AliasNotMatchedException(String message) {
            super(message);
        }
    }

    /**
     * throws this exception if the name is in a malformation
     *
     * @author Henry Yang
     * @since 2022-05-05
     */
    public static class NameMalFormatException extends RuntimeException {
        public NameMalFormatException(String message) {
            super(message);
        }
    }
}
