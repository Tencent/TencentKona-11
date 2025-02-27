/*
 * Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

import java.util.Arrays;
import jdk.test.lib.JDKToolLauncher;
import jdk.test.lib.process.OutputAnalyzer;
import jdk.test.lib.process.ProcessTools;
import utils.Utils;
import java.util.concurrent.locks.ReentrantLock;

/*
 * @test JstackTestStoppedThread
 * @summary for thread stopped with leaked Synchronizer, jstack should find and print warning
 * @modules java.base/jdk.internal.misc
 * @library /test/lib
 * @run main/othervm JstackTestStoppedThread
 */
public class JstackTestStoppedThread {
  /*
   * 1. main thread:  A.lock
   * 2. start thread "test_thread"
   * 3. test_thread: B.lock
   * 4. test_thread: A.lock
   * 5. main thread: check A.hasQueuedThreads and wait
   * 6. start test_thread_2 and block on B
   * 7. main thread: stop test_thread
   * 8. perform jstack on current process
   * 9. check output has warning information
   */
  public static void main(String[] args) throws Exception {
    ReentrantLock A = new ReentrantLock();
    ReentrantLock B = new ReentrantLock();
    try {
      // step1
      A.lock();

      // step2,3,4
      Runnable r1 = new Runnable() {
        public void run(){
          B.lock();
          A.lock();
          B.unlock();
        }
      };
      Thread thread = new Thread(r1, "test-thread");
      thread.start();

      // step5
      while (A.hasQueuedThreads() == false) {
        System.out.println("A still waiting query thread");
        Thread.sleep(1);
      }

      // step6
      Runnable r2 = new Runnable() {
        public void run(){
          B.lock();
          System.out.println("thread2 get B");
          B.unlock();
        }
      };
      Thread thread2 = new Thread(r2, "test-thread2");
      thread2.start();

      // step7
      thread.stop();
      while (A.hasQueuedThreads() == true) {
        System.out.println("A still waiting thread finish");
        Thread.sleep(1);
      }

      // step8
      ProcessBuilder processBuilder = new ProcessBuilder();
      JDKToolLauncher launcher = JDKToolLauncher.createUsingTestJDK("jstack");
      launcher.addToolArg(Long.toString(ProcessTools.getProcessId()));
      processBuilder.command(launcher.getCommand());
      System.out.println(Arrays.toString(processBuilder.command().toArray()).replace(",", ""));
      OutputAnalyzer output = ProcessTools.executeProcess(processBuilder);
      System.out.println(output.getOutput());
      output.shouldContain("is already stopped without unlock");
    } finally{
      A.unlock();
    }
  }
}
