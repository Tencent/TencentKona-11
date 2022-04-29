/*
 * Copyright (c) 2005, 2021, Oracle and/or its affiliates. All rights reserved.
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

import static jdk.test.lib.Asserts.assertTrue;
import static jdk.test.lib.Asserts.assertFalse;
import static jdk.test.lib.Asserts.fail;

import java.io.File;
import java.nio.file.Files;
import java.util.Arrays;
import java.util.List;

import jdk.test.lib.hprof.HprofParser;
import jdk.testlibrary.JDKToolLauncher;
import jdk.testlibrary.OutputAnalyzer;
import jdk.testlibrary.ProcessTools;

/*
 * @test
 * @summary Unit test for jmap utility
 * @key intermittent
 * @library /lib/testlibrary
 * @library /test/lib
 * @build jdk.testlibrary.*
 * @build jdk.test.lib.hprof.*
 * @build jdk.test.lib.hprof.model.*
 * @build jdk.test.lib.hprof.parser.*
 * @build jdk.test.lib.hprof.util.*
 * @run main/timeout=240 BasicJMapTest
 */
public class BasicJMapTest {

    private static ProcessBuilder processBuilder = new ProcessBuilder();

    public static void main(String[] args) throws Exception {
        testHisto();
        testHistoLive();
        testHistoAll();
        testHistoToFile();
        testHistoLiveToFile();
        testHistoAllToFile();
        testFinalizerInfo();
        testClstats();
        testDump();
        testDumpLive();
        testDumpAll();
        testDumpCompressed();
    }

    private static void testHisto() throws Exception {
        OutputAnalyzer output = jmap("-histo:");
        output.shouldHaveExitValue(0);
        OutputAnalyzer output1 = jmap("-histo");
        output1.shouldHaveExitValue(0);
    }

    private static void testHistoLive() throws Exception {
        OutputAnalyzer output = jmap("-histo:live");
        output.shouldHaveExitValue(0);
    }

    private static void testHistoAll() throws Exception {
        OutputAnalyzer output = jmap("-histo:all");
        output.shouldHaveExitValue(0);
    }

    private static void testHistoToFile() throws Exception {
        histoToFile(false);
    }

    private static void testHistoLiveToFile() throws Exception {
        histoToFile(true);
    }

    private static void testHistoAllToFile() throws Exception {
        boolean explicitAll = true;
        histoToFile(false, explicitAll);
    }

    private static void histoToFile(boolean live) throws Exception {
        boolean explicitAll = false;
        histoToFile(live, explicitAll);
    }

    private static void histoToFile(boolean live, boolean explicitAll) throws Exception {
        if (live == true && explicitAll == true) {
            fail("Illegal argument setting for jmap -histo");
        }
        File file = new File("jmap.histo.file" + System.currentTimeMillis() + ".histo");
        if (file.exists()) {
            file.delete();
        }
        OutputAnalyzer output;
        if (live) {
            output = jmap("-histo:live,file=" + file.getName());
        } else if (explicitAll == true) {
            output = jmap("-histo:all,file=" + file.getName());
        } else {
            output = jmap("-histo:file=" + file.getName());
        }
        output.shouldHaveExitValue(0);
        output.shouldContain("Heap inspection file created");
        file.delete();
    }

    private static void testHistoParallelZero() throws Exception {
        OutputAnalyzer output = jmap("-histo:parallel=0");
        output.shouldHaveExitValue(0);
    }

    private static void testHistoParallel() throws Exception {
        OutputAnalyzer output = jmap("-histo:parallel=2");
        output.shouldHaveExitValue(0);
    }

    private static void testHistoNonParallel() throws Exception {
        OutputAnalyzer output = jmap("-histo:parallel=1");
        output.shouldHaveExitValue(0);
    }

    private static void testFinalizerInfo() throws Exception {
        OutputAnalyzer output = jmap("-finalizerinfo");
        output.shouldHaveExitValue(0);
    }

    private static void testClstats() throws Exception {
        OutputAnalyzer output = jmap("-clstats");
        output.shouldHaveExitValue(0);
    }

    private static void testDump() throws Exception {
        dump(false);
    }

    private static void testDumpLive() throws Exception {
        dump(true);
    }

    private static void testDumpAll() throws Exception {
        boolean explicitAll = true;
        dump(false, explicitAll, false);
    }

    private static void dump(boolean live) throws Exception {
        boolean explicitAll = false;
        dump(live, explicitAll, false);
    }

    private static void testDumpCompressed() throws Exception {
        dump(true, false, true);
    }

    private static void dump(boolean live, boolean explicitAll, boolean compressed) throws Exception {
        if (live == true && explicitAll == true) {
          fail("Illegal argument setting for jmap -dump");
        }
        String filePath = "jmap.dump" + System.currentTimeMillis() + ".hprof";
        if (compressed) {
            filePath = filePath + ".gz";
        }
        File dump = new File(filePath);
        if (dump.exists()) {
            dump.delete();
        }
        OutputAnalyzer output;
        if (live) {
            output = jmap("-dump:live,format=b,file=" + dump.getName());
        } else if (explicitAll == true) {
            output = jmap("-dump:all,format=b,file=" + dump.getName());
        } else if (compressed == true) {
            output = jmap("-dump:all,format=b,gz=1,file=" + dump.getName());
        } else {
            output = jmap("-dump:format=b,file=" + dump.getName());
        }

        output.shouldHaveExitValue(0);
        output.shouldContain("Heap dump file created");
        verifyDumpFile(dump);
        dump.delete();
    }

    private static void verifyDumpFile(File dump) {
        assertTrue(dump.exists() && dump.isFile(), "Could not create dump file " + dump.getAbsolutePath());
        try {
            File out = HprofParser.parse(dump);

            assertTrue(out != null && out.exists() && out.isFile(),
                       "Could not find hprof parser output file");
            List<String> lines = Files.readAllLines(out.toPath());
            assertTrue(lines.size() > 0, "hprof parser output file is empty");
            for (String line : lines) {
                assertFalse(line.matches(".*WARNING(?!.*Failed to resolve " +
                                         "object.*constantPoolOop.*).*"));
            }

            out.delete();
        } catch (Exception e) {
            e.printStackTrace();
            fail("Could not parse dump file " + dump.getAbsolutePath());
        }
    }

    private static OutputAnalyzer jmap(String... toolArgs) throws Exception {
        JDKToolLauncher launcher = JDKToolLauncher.createUsingTestJDK("jmap");
        if (toolArgs != null) {
            for (String toolArg : toolArgs) {
                launcher.addToolArg(toolArg);
            }
        }
        launcher.addToolArg(Long.toString(ProcessTools.getProcessId()));

        processBuilder.command(launcher.getCommand());
        System.out.println(Arrays.toString(processBuilder.command().toArray()));
        OutputAnalyzer output = ProcessTools.executeProcess(processBuilder);
        System.out.println(output.getOutput());

        return output;
    }

}
