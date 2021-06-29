/*
 *
 * Copyright (C) 2021 THL A29 Limited, a Tencent company. All rights reserved.
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

/*
 * @test
 * @requires vm.fiber & vm.hasJFR
 * @requires vm.compMode != "Xint"
 * @summary testing of WB::forceNMethodSweep with virtual thread
 * @library /test/lib /
 * @modules java.base/jdk.internal.misc
 * @build sun.hotspot.WhiteBox
 * @run driver ClassFileInstaller sun.hotspot.WhiteBox
 *                                sun.hotspot.WhiteBox$WhiteBoxPermission
 * @run main/othervm -Xbootclasspath/a:. -XX:+UnlockDiagnosticVMOptions
 *                   -XX:-TieredCompilation -XX:+WhiteBoxAPI -XX:-UseOnStackReplacement
 *                   -XX:CompileCommand=compileonly,compiler.whitebox.ForceNMethodSweepTest_VT::foo
 *                   -XX:-BackgroundCompilation -XX:-ThreadLocalHandshakes -XX:-Inline
 *                   compiler.whitebox.ForceNMethodSweepTest_VT
 *
 */

package compiler.whitebox;
import jdk.test.lib.Asserts;
import jdk.internal.misc.VirtualThreads;
import sun.hotspot.WhiteBox;
import java.lang.reflect.Method;

import java.util.List;
import jdk.jfr.Recording;
import jdk.jfr.consumer.RecordedEvent;
import jdk.test.lib.jfr.EventNames;
import jdk.test.lib.jfr.Events;

/*
 * The whole test flow is
 * 1. trigger method compilation
 * 2. invoke method in VT and park VT (keep compiled method only used in VT).
 * 3. try force sweep with guaranteedSweep and check compiled method is not flushed.
 * 4. finish VT and force sweep again with guaranteedSweep, chekc compiled method is flushed.
 */
public class ForceNMethodSweepTest_VT {
    private static final String EVENT_NAME = EventNames.CodeSweeperStatistics;
    private static final WhiteBox WHITE_BOX = WhiteBox.getWhiteBox();
    private static volatile boolean VT_parked_flag = false;
    public static void main(String[] args) throws Exception {
       test();
    }

    public static void foo(boolean b) {
       VT_parked_flag = true;
       VirtualThreads.park();
    }

    static void test() throws Exception {
        Method foo_method = ForceNMethodSweepTest_VT.class.getMethod("foo", boolean.class);
        Runnable warmup = new Runnable() {
            public void run() {
                for (int i = 0; i < 10000; i++) {
                  VirtualThreads.unpark(Thread.currentThread());
                  foo(true);
                  VT_parked_flag = false;
                }
            }
        };
        guaranteedSweep();
        Thread vt1 = Thread.ofVirtual().start(warmup);
        vt1.join();

        Asserts.assertEQ(WHITE_BOX.isMethodCompiled(foo_method), true, "foo is not compiled");
        Runnable target = new Runnable() {
            public void run() {
                foo(true);
            }
        };
        System.out.println("before start vt");
        VT_parked_flag = false;
        Thread vt = Thread.ofVirtual().start(target);
        // wait enter park status
        while (true) {
            if (VT_parked_flag) {
                break;
            }
            Thread.sleep(1);
        }
        System.out.println("after vt park");
        Asserts.assertEQ(WHITE_BOX.isMethodCompiled(foo_method), true, "foo must be compiled");

        Recording recording = new Recording();
        recording.enable(EVENT_NAME).with("period", "endChunk");
        recording.start();

        WHITE_BOX.deoptimizeMethod(foo_method, false);
        System.out.println("before first guaranteedSweep");
        guaranteedSweep();
        System.out.println("after first guaranteedSweep");

        recording.stop();
        List<RecordedEvent> events = Events.fromRecording(recording);
        Events.hasEvents(events);
        for (RecordedEvent event : events) {
            Events.assertField(event, "methodReclaimedCount").equal(0);
        }

        VirtualThreads.unpark(vt);
        vt.join();
        System.out.println("before second guaranteedSweep");

        recording = new Recording();
        recording.enable(EVENT_NAME).with("period", "endChunk");
        recording.start();
        guaranteedSweep();
        recording.stop();
        events = Events.fromRecording(recording);
        Events.hasEvents(events);
        for (RecordedEvent event : events) {
            Events.assertField(event, "methodReclaimedCount").equal(1);
        }

        Asserts.assertEQ(WHITE_BOX.getMethodCompilationLevel(foo_method), 0, "foo_method is not flushed");
        System.out.println("after second guaranteedSweep");
    }

    private static void guaranteedSweep() {
        // not entrant -> ++stack_traversal_mark -> zombie -> flushed
        for (int i = 0; i < 5; ++i) {
            WHITE_BOX.fullGC();
            WHITE_BOX.forceNMethodSweep();
        }
    }
}
