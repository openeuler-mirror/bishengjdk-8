/*
 * Copyright (c) 2016, Oracle and/or its affiliates. All rights reserved.
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

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

/**
 * Generates sun.nio.fs.UnixConstants class
 */
static const char* cnames[]={
    // open flags
    "O_RDONLY",
    "O_WRONLY",
    "O_RDWR",
    "O_APPEND",
    "O_CREAT",
    "O_EXCL",
    "O_TRUNC",
    "O_SYNC",
    "O_DSYNC",
    "O_NOFOLLOW",
    // mode masks
    "S_IRUSR",
    "S_IWUSR",
    "S_IXUSR",
    "S_IRGRP",
    "S_IWGRP",
    "S_IXGRP",
    "S_IROTH",
    "S_IWOTH",
    "S_IXOTH",
    "S_IFMT",
    "S_IFREG",
    "S_IFDIR",
    "S_IFLNK",
    "S_IFCHR",
    "S_IFBLK",
    "S_IFIFO",
    "S_IAMB",
    // access modes
    "R_OK",
    "W_OK",
    "X_OK",
    "F_OK",

    // errors
    "ENOENT",
    "EACCES",
    "EEXIST",
    "ENOTDIR",
    "EINVAL",
    "EXDEV",
    "EISDIR",
    "ENOTEMPTY",
    "ENOSPC",
    "EAGAIN",
    "ENOSYS",
    "ELOOP",
    "EROFS",
    "ENODATA",
    "ERANGE",
    "EMFILE",

    // flags used with openat/unlinkat/etc.
    "AT_SYMLINK_NOFOLLOW",
    "AT_REMOVEDIR"
};
static void out(const char* s) {
    printf("%s\n", s);
}

static void declTemp(const char* name) {
    printf("    private static int p%s=0;\n",name);
}

static void declConst(const char* name) {
    printf("    static final int %s = p%s;\n", name, name);
}

static void init() {
    out("    private static native void init();");
    out("    static {");
    out("        AccessController.doPrivileged(new PrivilegedAction<Void>() {");
    out("            public Void run() {");
    out("                System.loadLibrary(\"nio\");");
    out("                return null;");
    out("        }});");
    out("        init();");
    out("    }");
}

int main(int argc, const char* argv[]) {
    int i;
    out("// AUTOMATICALLY GENERATED FILE - DO NOT EDIT                                  ");
    out("package sun.nio.fs;                                                            ");
    out("import java.security.AccessController;                                         ");
    out("import java.security.PrivilegedAction;                                         ");
    out("class UnixConstants {                                                          ");
    out("    private UnixConstants() { }                                                ");

    // define private intermediate constants
    for(i=0; i<(int)sizeof(cnames)/sizeof(cnames[0]);i++)
        declTemp(cnames[i]);

    init();

    // define real unix constants
    for(i=0; i<(int)sizeof(cnames)/sizeof(cnames[0]);i++)
        declConst(cnames[i]);

    out("}                                                                              ");

    return 0;
}
