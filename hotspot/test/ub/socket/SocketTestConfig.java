/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
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

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardOpenOption;

public final class SocketTestConfig {
    private static final String FILE_NAME = "UBSocket.conf";
    private static final String FILE_CONTENT =
        "sun/nio/ch/SocketChannelImpl.connect\n" +
        "sun/nio/ch/SocketChannelImpl.checkConnect\n" +
        "sun/nio/ch/ServerSocketChannelImpl.accept\n";

    private SocketTestConfig() {
    }

    public static String ensureSharedConfig() throws IOException {
        return writeConfig(FILE_NAME, FILE_CONTENT);
    }

    public static String writeConfig(String fileName, String content) throws IOException {
        Path baseDir = Paths.get(System.getProperty("test.classes", "."));
        Path file = baseDir.resolve(fileName);
        Files.write(file, content.getBytes(StandardCharsets.UTF_8),
                    StandardOpenOption.CREATE,
                    StandardOpenOption.TRUNCATE_EXISTING,
                    StandardOpenOption.WRITE);
        return file.toString();
    }
}
