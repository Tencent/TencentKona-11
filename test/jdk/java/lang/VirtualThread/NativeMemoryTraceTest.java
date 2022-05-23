/*
 *
 * Copyright (C) 2020 THL A29 Limited, a Tencent company. All rights reserved.
 * DO NOT ALTER OR REMOVE NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation. THL A29 Limited designates
 * this particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License version 2 for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

/**
 * @test
 * @library /test/lib
 * @run main/othervm NativeMemoryTraceTest
 * @summary Print the information of the native memory including Coroutine
 */

import java.io.IOException;
import java.util.concurrent.*;
import jdk.test.lib.process.OutputAnalyzer;
import jdk.test.lib.process.ProcessTools;
import jdk.test.lib.JDKToolFinder;
import java.lang.reflect.Field;

public class NativeMemoryTraceTest {
    static final CountDownLatch latch = new CountDownLatch(1);

    public static class SimpleVirtualTread {
        public static void main(String... args) throws Exception {
            Runnable r1 = new Runnable() {
                public void run() {
                    try {
                        latch.await();
                    } catch (InterruptedException e) {
                        e.printStackTrace();
                    }
                }
            };

            Thread thread1 = Thread.ofVirtual().name("vt1").start(r1);
            thread1.join();
        }
    }

    public static void main(String[] args) throws Throwable {
        // Start a thread associated with a coroutine
        ProcessBuilder simplePb = ProcessTools.createJavaProcessBuilder("-XX:NativeMemoryTracking=detail", NativeMemoryTraceTest.SimpleVirtualTread.class.getName());
        long simplePid = simplePb.start().toHandle().pid();
        // Start a thread, using 'jcmd' to observe the summary of nmt
        ProcessBuilder outputPb = new ProcessBuilder();
        outputPb.command(new String[]{JDKToolFinder.getJDKTool("jcmd"), String.valueOf(simplePid), "VM.native_memory", "summary"});
        OutputAnalyzer output = new OutputAnalyzer(outputPb.start());
        output.shouldContain("CoroutineStack");
        output.shouldContain("Coroutine");
        output.shouldNotContain("Unknown");
        latch.countDown();
    }
}
