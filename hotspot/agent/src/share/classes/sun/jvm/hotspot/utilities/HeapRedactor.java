/*
 * Copyright (c) 2023, Huawei Technologies Co., Ltd. All rights reserved.
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

package sun.jvm.hotspot.utilities;

import sun.jvm.hotspot.oops.TypeArray;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.IOException;
import java.util.HashMap;
import java.util.Locale;
import java.util.Map;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;

public class HeapRedactor {
    public enum HeapDumpRedactLevel {
        REDACT_UNKNOWN,
        REDACT_OFF,
        REDACT_NAMES,
        REDACT_BASIC,
        REDACT_DIYRULES,
        REDACT_ANNOTATION,
        REDACT_FULL
    }

    private HeapDumpRedactLevel redactLevel;
    private Map<String, String> redactNameTable;
    private Map<String, Map<String, String>> redactClassTable;
    private  String redactClassFullName = null;
    private Map<Long, String> redactValueTable;
    private RedactVectorNode headerNode;
    private RedactVectorNode currentNode;

    private RedactParams redactParams;

    public static final String HEAP_DUMP_REDACT_PREFIX = "HeapDumpRedact=";
    public static final String REDACT_MAP_PREFIX = "RedactMap=";
    public static final String REDACT_MAP_FILE_PREFIX = "RedactMapFile=";
    public static final String REDACT_CLASS_PATH_PREFIX = "RedactClassPath=";

    public static final String REDACT_UNKNOWN_STR = "UNKNOWN";
    public static final String REDACT_OFF_STR = "OFF";
    public static final String REDACT_NAME_STR = "NAMES";
    public static final String REDACT_BASIC_STR = "BASIC";
    public static final String REDACT_DIYRULES_STR = "DIYRULES";
    public static final String REDACT_ANNOTATION_STR = "ANNOTATION";
    public static final String REDACT_FULL_STR = "FULL";

    public static final String REDACT_UNKNOWN_OPTION = REDACT_UNKNOWN_STR.toLowerCase(Locale.ROOT);
    public static final String REDACT_OFF_OPTION = REDACT_OFF_STR.toLowerCase(Locale.ROOT);
    public static final String REDACT_NAME_OPTION = REDACT_NAME_STR.toLowerCase(Locale.ROOT);
    public static final String REDACT_BASIC_OPTION = REDACT_BASIC_STR.toLowerCase(Locale.ROOT);
    public static final String REDACT_DIYRULES_OPTION = REDACT_DIYRULES_STR.toLowerCase(Locale.ROOT);
    public static final String REDACT_ANNOTATION_OPTION = REDACT_ANNOTATION_STR.toLowerCase(Locale.ROOT);
    public static final String REDACT_FULL_OPTION = REDACT_FULL_STR.toLowerCase(Locale.ROOT);

    public static final int PATH_MAX = 4096;
    public static final int REDACT_VECTOR_SIZE = 1024;

    public HeapRedactor(String options) {
        redactLevel = HeapDumpRedactLevel.REDACT_UNKNOWN;
        redactNameTable = null;
        redactClassTable = null;
        redactValueTable = null;
        init(options);
    }

    public HeapRedactor(RedactParams redactParams) {
        this.redactParams = redactParams;
        redactLevel = HeapDumpRedactLevel.REDACT_UNKNOWN;
        redactNameTable = null;
        redactClassTable = null;
        redactValueTable = null;
        init(null);
    }

    private void init(String options) {
        if (redactLevel == HeapDumpRedactLevel.REDACT_UNKNOWN) {
            initHeapdumpRedactLevel(options);
        }
    }

    public HeapDumpRedactLevel getHeapDumpRedactLevel() {
        return redactLevel;
    }

    public String getRedactLevelString() {
        switch (redactLevel) {
            case REDACT_BASIC:
                return REDACT_BASIC_STR;
            case REDACT_NAMES:
                return REDACT_NAME_STR;
            case REDACT_FULL:
                return REDACT_FULL_STR;
            case REDACT_DIYRULES:
                return REDACT_DIYRULES_STR;
            case REDACT_ANNOTATION:
                return REDACT_ANNOTATION_STR;
            case REDACT_OFF:
                return REDACT_OFF_STR;
            case REDACT_UNKNOWN:
            default:
                return REDACT_UNKNOWN_STR;
        }
    }

    public String lookupRedactName(String name) {
        if (redactNameTable == null) {
            return null;
        }
        return redactNameTable.get(name);
    }

    public void recordTypeArray(TypeArray oop) {
        int tmp_index = currentNode.getCurrentIndex();
        if(tmp_index == REDACT_VECTOR_SIZE){
            RedactVectorNode newNode = new RedactVectorNode();
            List<TypeArray> list = new ArrayList<>(REDACT_VECTOR_SIZE);
            newNode.setTypeArrayList(list);
            newNode.setNext(null);
            newNode.setCurrentIndex(0);
            tmp_index = 0;
            currentNode.setNext(newNode);
            currentNode = newNode;
        }
        currentNode.getTypeArrayList().add(tmp_index, oop);
        tmp_index++;
        currentNode.setCurrentIndex(tmp_index);

    }

    public RedactVectorNode getHeaderNode(){
        return headerNode;
    }

    public void recordRedactAnnotationValue(Long addr, String value) {
        redactValueTable.put(addr, value);
    }

    public Optional<String> lookupRedactAnnotationValue(Long addr){
        return Optional.ofNullable(redactValueTable == null ? null : redactValueTable.get(addr));
    }

    public String getRedactAnnotationClassPath(){
        return redactParams.getRedactClassPath();
    }

    public Optional<Map<String, String>> getRedactRulesTable(String key) {
        return Optional.<Map<String, String>>ofNullable(redactClassTable == null ? null: redactClassTable.get(key));
    }

    public HeapDumpRedactLevel initHeapdumpRedactLevel(String options) {
        RedactParams customizedParams = parseRedactOptions(options);

        if (customizedParams.isEnableRedact() || this.redactParams == null) {
            this.redactParams = customizedParams;
        }

        if (redactParams.heapDumpRedact == null) {
            redactLevel = HeapDumpRedactLevel.REDACT_OFF;
        } else {
            if (REDACT_BASIC_OPTION.equals(redactParams.heapDumpRedact)) {
                redactLevel = HeapDumpRedactLevel.REDACT_BASIC;
            } else if (REDACT_NAME_OPTION.equals(redactParams.heapDumpRedact)) {
                redactLevel = HeapDumpRedactLevel.REDACT_NAMES;
                initRedactMap();
            } else if (REDACT_FULL_OPTION.equals(redactParams.heapDumpRedact)) {
                redactLevel = HeapDumpRedactLevel.REDACT_FULL;
                initRedactMap();
            } else if (REDACT_DIYRULES_OPTION.equals(redactParams.heapDumpRedact)) {
                redactLevel = HeapDumpRedactLevel.REDACT_DIYRULES;
                initRedactMap();
                initRedactVector();
            } else if (REDACT_ANNOTATION_OPTION.equals(redactParams.heapDumpRedact)) {
                redactLevel = HeapDumpRedactLevel.REDACT_ANNOTATION;
                initRedactVector();
            } else {
                redactLevel = HeapDumpRedactLevel.REDACT_OFF;
            }
        }
        return redactLevel;
    }

    private void initRedactVector(){
        if(redactValueTable == null) {
            redactValueTable = new HashMap<>();
        }
        if(headerNode == null) {
            headerNode = new RedactVectorNode();
            List<TypeArray> list = new ArrayList<>(REDACT_VECTOR_SIZE);
            headerNode.setTypeArrayList(list);
            headerNode.setNext(null);
            headerNode.setCurrentIndex(0);
            currentNode = headerNode;
        }
    }

    private RedactParams parseRedactOptions(String optionStr) {
        RedactParams params = new RedactParams(REDACT_OFF_OPTION, null, null, null);
        if (optionStr != null) {
            String[] options = optionStr.split(",");
            for (String option : options) {
                if (option.startsWith(HEAP_DUMP_REDACT_PREFIX)) {
                    params.setAndCheckHeapDumpRedact(option.substring(HEAP_DUMP_REDACT_PREFIX.length()));
                } else if (option.startsWith(REDACT_MAP_PREFIX)) {
                    params.setRedactMap(option.substring(REDACT_MAP_PREFIX.length()));
                } else if (option.startsWith(REDACT_MAP_FILE_PREFIX)) {
                    params.setRedactMapFile(option.substring(REDACT_MAP_FILE_PREFIX.length()));
                }  else if (option.startsWith(REDACT_CLASS_PATH_PREFIX)) {
                    params.setRedactClassPath(option.substring(REDACT_CLASS_PATH_PREFIX.length()));
                }else{
                    // None matches
                }
            }
        }
        return params;
    }

    private void initRedactMap() {
        if (redactParams.redactMapFile != null) {
            readRedactMapFromFile(redactParams.redactMapFile);
        }
        if (redactParams.redactMap != null) {
            parseRedactMapStringDependOnMode(redactParams.redactMap, redactLevel);
        }
    }

    private void readRedactMapFromFile(String path) {
        if (path == null || path.isEmpty()) {
            // RedactMapFile=<file> not specified
        } else {
            if (path.length() >= PATH_MAX) {
                System.err.println("RedactMap File path is too long");
                return;
            }
            File file = new File(path);
            if (!file.exists() || !file.isFile()) {
                System.err.println("RedactMap File does not exist");
            }
            try (BufferedReader reader = new BufferedReader(new FileReader(path))) {
                String line;
                while ((line = reader.readLine()) != null) {
                    parseRedactMapStringDependOnMode(line, redactLevel);
                }
            } catch (IOException e) {
                System.err.println("Encounter an error when reading " + path + " , skip processing RedactMap File.");
                e.printStackTrace();
                return;
            }
        }
    }

    private void parseRedactMapStringDependOnMode(String nameMapList, HeapDumpRedactLevel redactLevel) {
        if(redactLevel == HeapDumpRedactLevel.REDACT_DIYRULES) {
            parseRedactDiyRulesString(nameMapList);
        } else {
            parseRedactMapString(nameMapList);
        }
    }

    private void parseRedactMapString(String nameMapList) {
        if (redactNameTable == null) {
            redactNameTable = new HashMap<>();
        }
        String[] tokens = nameMapList.split("[,;\\s+]");
        for (String token : tokens) {
            String[] pair = token.split(":");
            if (pair.length == 2) {
                redactNameTable.put(pair[0], pair[1]);
            }
        }
    }

    private void parseRedactDiyRulesString(String nameMapList) {
        if (redactClassTable == null) {
            redactClassTable = new HashMap<>();
        }
        Map<String, String> redactRulesTable = redactClassFullName == null ? null : redactClassTable.get(redactClassFullName);
        String[] tokens = nameMapList.split("[,;\\s+]");
        for (String token : tokens) {
            String[] pair = token.split(":");
            if (pair.length == 1) {
                redactClassFullName = pair[0].replace(".", "/");
                redactRulesTable = redactClassTable.get(redactClassFullName);
                if(redactRulesTable == null) {
                    redactRulesTable = new HashMap<>();
                    redactClassTable.put(redactClassFullName, redactRulesTable);
                }
            }
            if (pair.length == 2 && redactRulesTable != null) {
                redactRulesTable.put(pair[0], pair[1]);
            }
        }
    }

    public static class RedactParams {
        private String heapDumpRedact;
        private String redactMap;
        private String redactMapFile;
        private String redactClassPath;
        private boolean enableRedact = false;

        public RedactParams() {
        }

        public RedactParams(String heapDumpRedact, String redactMap, String redactMapFile, String redactClassPath) {
            this.heapDumpRedact = heapDumpRedact;
            this.redactMap = redactMap;
            this.redactMapFile = redactMapFile;
            this.redactClassPath = redactClassPath;
        }

        @Override
        public String toString() {
            StringBuilder builder = new StringBuilder();
            if (heapDumpRedact != null) {
                builder.append(HEAP_DUMP_REDACT_PREFIX);
                builder.append(heapDumpRedact);
                builder.append(",");
            }
            if (redactMap != null) {
                builder.append(REDACT_MAP_PREFIX);
                builder.append(redactMap);
                builder.append(",");
            }
            if (redactMapFile != null) {
                builder.append(REDACT_MAP_FILE_PREFIX);
                builder.append(redactMapFile);
                builder.append(",");
            }
            if (redactClassPath != null) {
                builder.append(REDACT_CLASS_PATH_PREFIX);
                builder.append(redactClassPath);
            }
            return builder.toString();
        }

        public String getHeapDumpRedact() {
            return heapDumpRedact;
        }

        public boolean setAndCheckHeapDumpRedact(String heapDumpRedact) {
            if (!checkLauncherHeapdumpRedactSupport(heapDumpRedact)) {
                return false;
            }
            this.heapDumpRedact = heapDumpRedact;
            this.enableRedact = true;
            return true;
        }

        public String getRedactMap() {
            return redactMap;
        }

        public void setRedactMap(String redactMap) {
            this.redactMap = redactMap;
        }

        public String getRedactMapFile() {
            return redactMapFile;
        }

        public void setRedactMapFile(String redactMapFile) {
            this.redactMapFile = redactMapFile;
        }

        public String getRedactClassPath() {
            return redactClassPath;
        }

        public void setRedactClassPath(String redactClassPath) {
            this.redactClassPath = redactClassPath;
        }

        public static boolean checkLauncherHeapdumpRedactSupport(String value) {
            String[] validValues = {REDACT_BASIC_OPTION, REDACT_NAME_OPTION, REDACT_FULL_OPTION, REDACT_DIYRULES_OPTION, REDACT_ANNOTATION_OPTION, REDACT_OFF_OPTION};
            for (String validValue : validValues) {
                if (validValue.equals(value)) {
                    return true;
                }
            }
            return false;
        }

        public boolean isEnableRedact() {
            return enableRedact;
        }

        public void setEnableRedact(boolean enableRedact) {
            this.enableRedact = enableRedact;
        }
    }

     public class RedactVectorNode{
        private List<TypeArray> typeArrayList;
        private RedactVectorNode next;
        private int currentIndex;

        public List<TypeArray> getTypeArrayList() {
            return typeArrayList;
        }

        public void setTypeArrayList(List<TypeArray> list) {
            this.typeArrayList = list;
        }

        public RedactVectorNode getNext() {
            return next;
        }

        public void setNext(RedactVectorNode next) {
            this.next = next;
        }

        public int getCurrentIndex() {
            return currentIndex;
        }

        public void setCurrentIndex(int index) {
            this.currentIndex = index;
        }
    }
}
