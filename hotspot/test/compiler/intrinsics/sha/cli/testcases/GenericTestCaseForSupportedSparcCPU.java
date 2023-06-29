/*
 * Copyright (c) 2014, Oracle and/or its affiliates. All rights reserved.
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

import com.oracle.java.testlibrary.ExitCode;
import com.oracle.java.testlibrary.Platform;
import com.oracle.java.testlibrary.cli.CommandLineOptionTest;
import com.oracle.java.testlibrary.cli.predicate.AndPredicate;

/**
 * Generic test case for SHA-related options targeted to SPARC CPUs which
 * support instructions required by the tested option.
 */
public class GenericTestCaseForSupportedSparcCPU extends
        DigestOptionsBase.TestCase {
    public GenericTestCaseForSupportedSparcCPU(String optionName) {
        super(optionName, new AndPredicate(Platform::isSparc,
                DigestOptionsBase.getPredicateForOption(optionName)));
    }

    @Override
    protected void verifyWarnings() throws Throwable {
        // Verify that there are no warning when option is explicitly enabled.
        CommandLineOptionTest.verifySameJVMStartup(null, new String[] {
                        DigestOptionsBase.getWarningForUnsupportedCPU(optionName)
                }, ExitCode.OK,
                DigestOptionsBase.UNLOCK_DIAGNOSTIC_VM_OPTIONS,
                CommandLineOptionTest.prepareBooleanFlag(optionName, true));

        // Verify that option could be disabled even if +UseSHA was passed to
        // JVM.
        CommandLineOptionTest.verifySameJVMStartup(null, new String[] {
                        DigestOptionsBase.getWarningForUnsupportedCPU(optionName)
                }, ExitCode.OK,
                DigestOptionsBase.UNLOCK_DIAGNOSTIC_VM_OPTIONS,
                CommandLineOptionTest.prepareBooleanFlag(
                        DigestOptionsBase.USE_SHA_OPTION, true),
                CommandLineOptionTest.prepareBooleanFlag(optionName, false));

        // Verify that it is possible to enable the tested option and disable
        // all SHA intrinsics via -UseSHA without any warnings.
        CommandLineOptionTest.verifySameJVMStartup(null, new String[] {
                        DigestOptionsBase.getWarningForUnsupportedCPU(optionName)
                }, ExitCode.OK,
                DigestOptionsBase.UNLOCK_DIAGNOSTIC_VM_OPTIONS,
                CommandLineOptionTest.prepareBooleanFlag(
                        DigestOptionsBase.USE_SHA_OPTION, false),
                CommandLineOptionTest.prepareBooleanFlag(optionName, true));
    }

    @Override
    protected void verifyOptionValues() throws Throwable {
        // Verify that on supported CPU option is enabled by default.
        CommandLineOptionTest.verifyOptionValueForSameVM(optionName, "true",
                                DigestOptionsBase.UNLOCK_DIAGNOSTIC_VM_OPTIONS);

        // Verify that it is possible to explicitly enable the option.
        CommandLineOptionTest.verifyOptionValueForSameVM(optionName, "true",
                DigestOptionsBase.UNLOCK_DIAGNOSTIC_VM_OPTIONS,
                CommandLineOptionTest.prepareBooleanFlag(optionName, true));

        // Verify that it is possible to explicitly disable the option.
        CommandLineOptionTest.verifyOptionValueForSameVM(optionName, "false",
                DigestOptionsBase.UNLOCK_DIAGNOSTIC_VM_OPTIONS,
                CommandLineOptionTest.prepareBooleanFlag(optionName, false));

        // verify that option is disabled when -UseSHA was passed to JVM.
        CommandLineOptionTest.verifyOptionValueForSameVM(optionName, "false",
                DigestOptionsBase.UNLOCK_DIAGNOSTIC_VM_OPTIONS,
                CommandLineOptionTest.prepareBooleanFlag(optionName, true),
                CommandLineOptionTest.prepareBooleanFlag(
                        DigestOptionsBase.USE_SHA_OPTION, false));

        // Verify that it is possible to explicitly disable the tested option
        // even if +UseSHA was passed to JVM.
        CommandLineOptionTest.verifyOptionValueForSameVM(optionName, "false",
                DigestOptionsBase.UNLOCK_DIAGNOSTIC_VM_OPTIONS,
                CommandLineOptionTest.prepareBooleanFlag(
                        DigestOptionsBase.USE_SHA_OPTION, true),
                CommandLineOptionTest.prepareBooleanFlag(optionName, false));
    }
}
