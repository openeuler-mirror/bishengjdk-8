#!/bin/sh

#
# Copyright (c) Huawei Technologies Co., Ltd. 2020. All rights reserved.
#

# @test
# @summary Unit test for jmap parallel heap inspection feature
# @library ../common
# @build SimpleApplication ShutdownSimpleApplication
# @run shell ParallelInspection.sh

. ${TESTSRC}/../common/CommonSetup.sh
. ${TESTSRC}/../common/ApplicationSetup.sh
# parallel num in G1GC
# Start application and use PORTFILE for coordination
PORTFILE="${TESTCLASSES}"/shutdown.port
startApplication SimpleApplication "${PORTFILE}" defineGC UseG1GC

# all return statuses are checked in this test
set +e

failed=0

${JMAP} -J-XX:+UsePerfData -histo:parallel=0 $appJavaPid
if [ $? != 0 ]; then failed=1; fi

${JMAP} -J-XX:+UsePerfData -histo:parallel=1 $appJavaPid
if [ $? != 0 ]; then failed=1; fi

${JMAP} -J-XX:+UsePerfData -histo:parallel=2 $appJavaPid
if [ $? != 0 ]; then failed=1; fi

${JMAP} -J-XX:+UsePerfData -histo:live,parallel=0 $appJavaPid
if [ $? != 0 ]; then failed=1; fi

${JMAP} -J-XX:+UsePerfData -histo:live,parallel=1 $appJavaPid
if [ $? != 0 ]; then failed=1; fi

${JMAP} -J-XX:+UsePerfData -histo:live,parallel=2 $appJavaPid
if [ $? != 0 ]; then failed=1; fi
set -e

stopApplication "${PORTFILE}"
waitForApplication

# parallel num in ParallelGC
# Start application and use PORTFILE for coordination
PORTFILE="${TESTCLASSES}"/shutdown.port
startApplication SimpleApplication "${PORTFILE}" defineGC UseParallelGC

# all return statuses are checked in this test
set +e

failed=0

${JMAP} -J-XX:+UsePerfData -histo:parallel=0 $appJavaPid
if [ $? != 0 ]; then failed=1; fi

${JMAP} -J-XX:+UsePerfData -histo:parallel=1 $appJavaPid
if [ $? != 0 ]; then failed=1; fi

${JMAP} -J-XX:+UsePerfData -histo:parallel=2 $appJavaPid
if [ $? != 0 ]; then failed=1; fi

${JMAP} -J-XX:+UsePerfData -histo:live,parallel=0 $appJavaPid
if [ $? != 0 ]; then failed=1; fi

${JMAP} -J-XX:+UsePerfData -histo:live,parallel=1 $appJavaPid
if [ $? != 0 ]; then failed=1; fi

${JMAP} -J-XX:+UsePerfData -histo:live,parallel=2 $appJavaPid
if [ $? != 0 ]; then failed=1; fi
set -e

stopApplication "${PORTFILE}"
waitForApplication

exit $failed
