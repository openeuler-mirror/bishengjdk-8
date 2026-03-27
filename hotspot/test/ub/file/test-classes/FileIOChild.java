/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
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
 */

import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;

public class FileIOChild {
    public static void main(String[] args) throws Exception {
        Path file = Paths.get(args[0]);
        String content = args[1];

        try {
            FileIOHelper.writeString(file, content);
            String got = FileIOHelper.readString(file);

            if (!content.equals(got)) {
                System.err.println("Content mismatch. expected=[" + content + "], got=[" + got + "]");
                System.exit(2);
            }

            System.out.println("File IO Test PASSED");
            System.exit(0);
        } finally {
            try { Files.deleteIfExists(file); } catch (Exception ignored) {}
        }
    }
}