/*
 * Copyright (c) 2005, 2013, Oracle and/or its affiliates. All rights reserved.
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
 *
 */

package sun.jvm.hotspot.tools;

import sun.jvm.hotspot.runtime.VM;
import sun.jvm.hotspot.utilities.HeapHprofBinWriter;
import sun.jvm.hotspot.debugger.JVMDebugger;
import sun.jvm.hotspot.utilities.HeapRedactor;

import java.io.IOException;

/*
 * This tool is used by the JDK jmap utility to dump the heap of the target
 * process/core as a HPROF binary file. It can also be used as a standalone
 * tool if required.
 */
public class HeapDumper extends Tool {

    private static String DEFAULT_DUMP_FILE = "heap.bin";

    // encrypt
    private static int SALT_MIN_LENGTH = 8;

    private String dumpFile;

    private HeapRedactor redactor;

    public HeapDumper(String dumpFile) {
        this.dumpFile = dumpFile;
    }

    public HeapDumper(String dumpFile, HeapRedactor redactor){
        this(dumpFile);
        this.redactor = redactor;
    }

    public HeapDumper(String dumpFile, JVMDebugger d) {
        super(d);
        this.dumpFile = dumpFile;
    }

    protected void printFlagsUsage() {
        System.out.println("    <no option>\tto dump heap to " +
            DEFAULT_DUMP_FILE);
        System.out.println("    -f <file>\tto dump heap to <file>");
        super.printFlagsUsage();
    }

    private String getVMRedactParameter(String name){
        VM vm = VM.getVM();
        VM.Flag flag = vm.getCommandLineFlag(name);
        if(flag == null){
            return null;
        }
        return flag.getCcstr();
    }

    // use HeapHprofBinWriter to write the heap dump
    public void run() {
        System.out.println("Dumping heap to " + dumpFile + " ...");
        try {
            String redactAuth = getVMRedactParameter("RedactPassword");
            boolean redactAuthFlag = true;
            if(redactAuth != null) {
                String[] auths = redactAuth.split(",");
                if(auths.length == 2) {
                    byte[] saltBytes = auths[1].getBytes("UTF-8");
                    if(saltBytes.length >= SALT_MIN_LENGTH) {
                        redactAuthFlag = (this.redactor != null && auths[0].equals(this.redactor.getRedactPassword()));
                    }
                }
            }
            HeapHprofBinWriter writer = new HeapHprofBinWriter();
            if(this.redactor != null && redactAuthFlag) {
                writer.setHeapRedactor(this.redactor);
                if(writer.getHeapDumpRedactLevel() != HeapRedactor.HeapDumpRedactLevel.REDACT_UNKNOWN){
                    System.out.println("HeapDump Redact Level = " + this.redactor.getRedactLevelString());
                }
            }else{
                resetHeapHprofBinWriter(writer);
            }
            writer.write(dumpFile);
            System.out.println("Heap dump file created");
        } catch (IOException ioe) {
            System.err.println(ioe.getMessage());
        }
    }

    private void resetHeapHprofBinWriter(HeapHprofBinWriter writer) {
        String redactStr = getVMRedactParameter("HeapDumpRedact");
        if(redactStr != null && !redactStr.isEmpty()){
            HeapRedactor.RedactParams redactParams = new HeapRedactor.RedactParams();
            if(HeapRedactor.REDACT_ANNOTATION_OPTION.equals(redactStr)){
                String classPathStr = getVMRedactParameter("RedactClassPath");
                redactStr = (classPathStr != null && !classPathStr.isEmpty()) ? redactStr : HeapRedactor.REDACT_OFF_OPTION;
                redactParams.setRedactClassPath(classPathStr);
            } else {
                String redactMapStr = getVMRedactParameter("RedactMap");
                redactParams.setRedactMap(redactMapStr);
                String redactMapFileStr = getVMRedactParameter("RedactMapFile");
                redactParams.setRedactMapFile(redactMapFileStr);
            }
            redactParams.setAndCheckHeapDumpRedact(redactStr);
            writer.setHeapRedactor(new HeapRedactor(redactParams));
        }
    }

    // JDK jmap utility will always invoke this tool as:
    //   HeapDumper -f <file> <args...>
    public static void main(String args[]) {
        String file = DEFAULT_DUMP_FILE;
        HeapRedactor heapRedactor = null;
        if (args.length > 2) {
            if (args[0].equals("-f")) {
                file = args[1];
                String[] newargs = new String[args.length-2];
                System.arraycopy(args, 2, newargs, 0, args.length-2);
                args = newargs;
            }
            if(args[0].equals("-r")){
                heapRedactor = new HeapRedactor(args[1]);
                String[] newargs = new String[args.length-2];
                System.arraycopy(args, 2, newargs, 0, args.length-2);
                args = newargs;
            }
        }

        HeapDumper dumper = heapRedactor == null? new HeapDumper(file) : new HeapDumper(file, heapRedactor);
        dumper.execute(args);
    }

}
