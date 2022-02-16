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

package jdk.jfr.internal.instrument;

import jdk.jfr.events.JavaNativeAllocationEvent;
import jdk.jfr.events.JavaNativeFreeEvent;
import jdk.jfr.events.JavaNativeReallocateEvent;

@JIInstrumentationTarget("jdk.internal.misc.Unsafe")
final class UnsafeInstrumentor {
    private UnsafeInstrumentor() {
    }

    @SuppressWarnings("deprecation")
    @JIInstrumentationMethod
    public long allocateMemory(long bytes) {
        JavaNativeAllocationEvent event = JavaNativeAllocationEvent.EVENT.get();
        if (!event.isEnabled()) {
            return allocateMemory(bytes);
        }
        long addr = 0;
        try {
            event.begin();
            addr = allocateMemory(bytes);
        } finally {
            event.end();
            if (event.shouldCommit()) {
                if (addr != 0) {
                    event.addr = addr;
                    event.allocationSize = bytes;
                }
                event.commit();
                event.reset();
            }
        }
        return addr;
    }

    @SuppressWarnings("deprecation")
    @JIInstrumentationMethod
    public long reallocateMemory(long address, long bytes) {
        JavaNativeReallocateEvent event = JavaNativeReallocateEvent.EVENT.get();
        if (!event.isEnabled()) {
            return reallocateMemory(address, bytes);
        }
        long addr = 0;
        try {
            event.begin();
            addr = reallocateMemory(address, bytes);
        } finally {
            event.end();
            if (event.shouldCommit()) {
                if (addr != 0) {
                    event.freeAddr = address;
                    event.allocAddr = addr;
                    event.allocationSize = bytes;
                }
                event.commit();
                event.reset();
            }
        }
        return addr;
    }

    @SuppressWarnings("deprecation")
    @JIInstrumentationMethod
    public void freeMemory(long address) {
        JavaNativeFreeEvent event = JavaNativeFreeEvent.EVENT.get();
        if (!event.isEnabled()) {
            freeMemory(address);
            return;
        }
     
        try {
            event.begin();
            freeMemory(address);
        } finally {
            event.end();
            if (event.shouldCommit()) {
                if (address != 0) {
                    event.addr = address;
                }
                event.commit();
                event.reset();
            }
        }
    }
}