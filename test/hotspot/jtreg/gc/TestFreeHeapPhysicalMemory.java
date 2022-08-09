/*
 * Copyright (C) 2021 THL A29 Limited, a Tencent company. All rights reserved.
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

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Optional;
import java.util.stream.Stream;

import jdk.test.lib.Asserts;
import jdk.test.lib.process.OutputAnalyzer;
import jdk.test.lib.process.ProcessTools;

/**
 * @test TestFreeHeapPhysicalMemory
 * @key gc
 * @requires (os.family == "linux")
 * @library /test/lib
 * @modules java.base/jdk.internal.misc
 *          java.management
 * @run main TestFreeHeapPhysicalMemory
 */
public class TestFreeHeapPhysicalMemory {

    private static String vmRSS() {
        // Read VmRSS from /proc/self/status
        String path = "/proc/self/status";
        Optional<String> o = Optional.empty();
        try (Stream<String> stream = Files.lines(Paths.get(path))) {
            o = stream.filter(line -> line.contains("VmRSS"))
                      .findFirst();
        } catch (IOException e) {
            e.printStackTrace();
        }
        if (!o.isPresent()) {
            return null;
        }
        String[] parts = o.get().replaceAll("\\s", "").split(":");
        return parts[1];
    }

    private static long getVmRSS() {
        String rss = vmRSS();
        return Long.parseLong(rss.split("kB")[0]);
    }

    public static void main(String[] args) throws Exception {
        ProcessBuilder pb = ProcessTools.createJavaProcessBuilder("-XX:+UseG1GC",
                                                                  "-Xms1g", "-Xmx1g",
                                                                  "-Xlog:gc+heap=debug",
                                                                  GCTest.class.getName(), "false");
        OutputAnalyzer output = new OutputAnalyzer(pb.start());
        output.shouldNotContain("Attempt heap shrinking, shrink_bytes");
        output.shouldHaveExitValue(0);

        pb = ProcessTools.createJavaProcessBuilder("-XX:+UseG1GC",
                                                   "-Xms1g", "-Xmx1g",
                                                   "-Xlog:gc+heap=debug",
                                                   "-XX:+FreeHeapPhysicalMemory",
                                                   GCTest.class.getName(), "true");
        output = new OutputAnalyzer(pb.start());
        output.shouldContain("Attempt heap shrinking, shrink_bytes");
        output.shouldHaveExitValue(0);

        pb = ProcessTools.createJavaProcessBuilder("-XX:+UseG1GC",
                                                   "-Xms1g", "-Xmx1g",
                                                   "-Xlog:gc+heap=debug",
                                                   "-XX:+FreeHeapPhysicalMemory",
                                                   "-XX:G1FreeOldMemoryThresholdPercentAfterFullGC=100",
                                                   GCTest.class.getName(), "true");
        output = new OutputAnalyzer(pb.start());
        output.shouldContain("Attempt heap shrinking, shrink_bytes");
        output.shouldHaveExitValue(0);
    }

    static class GCTest {
        private static final int LEN = 1024;
        private static ArrayList<Object[]> list = null;

        private static void populateArray() {
            list = new ArrayList(LEN);
            for (int i = 0; i < LEN; i++) {
                Object[] o = new Object[LEN];
                for (int j = 0; j < LEN; j++) {
                    o[j] = new byte[500];
                }
                list.add(o);
            }
        }

        private static void cleanArray() {
            list = null;
        }

        public static void main(String[] args) {
            populateArray();
            System.gc();
            long rssBefore = getVmRSS();
            cleanArray();
            System.gc();
            System.gc();
            long rssAfter = getVmRSS();
            System.out.println(rssAfter + " " + rssBefore);
            if (args[0].equals("true")) {
                Asserts.assertLT(rssAfter, rssBefore);
            } else if (args[0].equals("false")) {
                Asserts.assertGTE(rssAfter, rssBefore);
            }
        }
    }
}
