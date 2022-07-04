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
 * - @TestCaseID:provider/GetServiceBenchmark.java
 * - @TestCaseName:provider/GetServiceBenchmark.java
 * - @TestCaseType:Performance test
 * - @RequirementID:AR.SR.IREQ02758058.001.001
 * - @RequirementName:java.security.Provider.getService() is synchronized and became scalability bottleneck
 * - @Condition:JDK8u302及以后
 * - @Brief:测试provider.getService的性能
 *   -#step:创建jmh的maven项目mvn archetype:generate -DinteractiveMode=false -DarchetypeGroupId=org.openjdk.jmh -DarchetypeArtifactId=jmh-java-benchmark-archetype -DgroupId=org.openeuler.bench.security.provider -DartifactId=provider-benchmark -Dversion=1.0
 *   -#step2:删除项目中的多余文件rm -rf provider-benchmark/src/main/java/org/openeuler/bench/security/provider/MyBenchmark.java
 *   -#step3:将本文件拷贝进项目目录cp GetServiceBenchmark.java provider-benchmark/src/main/java/org/openeuler/bench/security/provider/
 *   -#step4:构建项目mvn install
 *   -#step5:运行测试java -jar target/benchmarks.jar GetServiceBenchmark
 * - @Expect:正常运行
 * - @Priority:Level 1
 */

package org.openeuler.bench.security.provider;

import com.sun.crypto.provider.SunJCE;

import org.openjdk.jmh.annotations.Benchmark;
import org.openjdk.jmh.annotations.BenchmarkMode;
import org.openjdk.jmh.annotations.Fork;
import org.openjdk.jmh.annotations.Measurement;
import org.openjdk.jmh.annotations.Mode;
import org.openjdk.jmh.annotations.Scope;
import org.openjdk.jmh.annotations.State;
import org.openjdk.jmh.annotations.Threads;
import org.openjdk.jmh.annotations.Warmup;

import java.security.Provider;
import java.util.concurrent.TimeUnit;

/**
 * Benchmark to test the performance of provider.getService in
 * high concurrency scenarios.
 *
 * @author Henry Yang
 * @since 2022-05-05
 */
@BenchmarkMode(Mode.Throughput)
@Fork(1)
@Threads(2000)
@Warmup(iterations = 3, time = 3, timeUnit = TimeUnit.SECONDS)
@Measurement(iterations = 5, time = 3, timeUnit = TimeUnit.SECONDS)
@State(Scope.Benchmark)
public class GetServiceBenchmark {
    private Provider provider = new SunJCE();

    @Benchmark
    public void getService() {
        try {
            provider.getService("Cipher", "RSA");
        } catch (Exception e) {
            e.printStackTrace();
        }
    }
}
