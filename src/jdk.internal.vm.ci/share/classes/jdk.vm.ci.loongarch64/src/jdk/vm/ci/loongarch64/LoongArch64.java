/*
 * Copyright (c) 2015, 2022, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2022, Loongson Technology. All rights reserved.
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
package jdk.vm.ci.loongarch64;

import java.nio.ByteOrder;
import java.util.EnumSet;

import jdk.vm.ci.code.Architecture;
import jdk.vm.ci.code.Register;
import jdk.vm.ci.code.Register.RegisterCategory;
import jdk.vm.ci.code.RegisterArray;
import jdk.vm.ci.meta.JavaKind;
import jdk.vm.ci.meta.PlatformKind;

/**
 * Represents the LoongArch64 architecture.
 */
public class LoongArch64 extends Architecture {

    public static final RegisterCategory CPU = new RegisterCategory("CPU");

    // General purpose CPU registers
    public static final Register zero = new Register(0, 0, "r0", CPU);
    public static final Register ra = new Register(1, 1, "r1", CPU);
    public static final Register tp = new Register(2, 2, "r2", CPU);
    public static final Register sp = new Register(3, 3, "r3", CPU);
    public static final Register a0 = new Register(4, 4, "r4", CPU);
    public static final Register a1 = new Register(5, 5, "r5", CPU);
    public static final Register a2 = new Register(6, 6, "r6", CPU);
    public static final Register a3 = new Register(7, 7, "r7", CPU);
    public static final Register a4 = new Register(8, 8, "r8", CPU);
    public static final Register a5 = new Register(9, 9, "r9", CPU);
    public static final Register a6 = new Register(10, 10, "r10", CPU);
    public static final Register a7 = new Register(11, 11, "r11", CPU);
    public static final Register t0 = new Register(12, 12, "r12", CPU);
    public static final Register t1 = new Register(13, 13, "r13", CPU);
    public static final Register t2 = new Register(14, 14, "r14", CPU);
    public static final Register t3 = new Register(15, 15, "r15", CPU);
    public static final Register t4 = new Register(16, 16, "r16", CPU);
    public static final Register t5 = new Register(17, 17, "r17", CPU);
    public static final Register t6 = new Register(18, 18, "r18", CPU);
    public static final Register t7 = new Register(19, 19, "r19", CPU);
    public static final Register t8 = new Register(20, 20, "r20", CPU);
    public static final Register rx = new Register(21, 21, "r21", CPU);
    public static final Register fp = new Register(22, 22, "r22", CPU);
    public static final Register s0 = new Register(23, 23, "r23", CPU);
    public static final Register s1 = new Register(24, 24, "r24", CPU);
    public static final Register s2 = new Register(25, 25, "r25", CPU);
    public static final Register s3 = new Register(26, 26, "r26", CPU);
    public static final Register s4 = new Register(27, 27, "r27", CPU);
    public static final Register s5 = new Register(28, 28, "r28", CPU);
    public static final Register s6 = new Register(29, 29, "r29", CPU);
    public static final Register s7 = new Register(30, 30, "r30", CPU);
    public static final Register s8 = new Register(31, 31, "r31", CPU);

    public static final Register SCR1 = t7;
    public static final Register SCR2 = t4;
    public static final Register v0 = a0;

    // @formatter:off
    public static final RegisterArray cpuRegisters = new RegisterArray(
        zero, ra,  tp,  sp,  a0,  a1,  a2,  a3,
        a4,   a5,  a6,  a7,  t0,  t1,  t2,  t3,
        t4,   t5,  t6,  t7,  t8,  rx,  fp,  s0,
        s1,   s2,  s3,  s4,  s5,  s6,  s7,  s8
    );
    // @formatter:on

    public static final RegisterCategory SIMD = new RegisterCategory("SIMD");

    // Simd registers
    public static final Register f0 = new Register(32, 0, "f0", SIMD);
    public static final Register f1 = new Register(33, 1, "f1", SIMD);
    public static final Register f2 = new Register(34, 2, "f2", SIMD);
    public static final Register f3 = new Register(35, 3, "f3", SIMD);
    public static final Register f4 = new Register(36, 4, "f4", SIMD);
    public static final Register f5 = new Register(37, 5, "f5", SIMD);
    public static final Register f6 = new Register(38, 6, "f6", SIMD);
    public static final Register f7 = new Register(39, 7, "f7", SIMD);
    public static final Register f8 = new Register(40, 8, "f8", SIMD);
    public static final Register f9 = new Register(41, 9, "f9", SIMD);
    public static final Register f10 = new Register(42, 10, "f10", SIMD);
    public static final Register f11 = new Register(43, 11, "f11", SIMD);
    public static final Register f12 = new Register(44, 12, "f12", SIMD);
    public static final Register f13 = new Register(45, 13, "f13", SIMD);
    public static final Register f14 = new Register(46, 14, "f14", SIMD);
    public static final Register f15 = new Register(47, 15, "f15", SIMD);
    public static final Register f16 = new Register(48, 16, "f16", SIMD);
    public static final Register f17 = new Register(49, 17, "f17", SIMD);
    public static final Register f18 = new Register(50, 18, "f18", SIMD);
    public static final Register f19 = new Register(51, 19, "f19", SIMD);
    public static final Register f20 = new Register(52, 20, "f20", SIMD);
    public static final Register f21 = new Register(53, 21, "f21", SIMD);
    public static final Register f22 = new Register(54, 22, "f22", SIMD);
    public static final Register f23 = new Register(55, 23, "f23", SIMD);
    public static final Register f24 = new Register(56, 24, "f24", SIMD);
    public static final Register f25 = new Register(57, 25, "f25", SIMD);
    public static final Register f26 = new Register(58, 26, "f26", SIMD);
    public static final Register f27 = new Register(59, 27, "f27", SIMD);
    public static final Register f28 = new Register(60, 28, "f28", SIMD);
    public static final Register f29 = new Register(61, 29, "f29", SIMD);
    public static final Register f30 = new Register(62, 30, "f30", SIMD);
    public static final Register f31 = new Register(63, 31, "f31", SIMD);

    public static final Register fv0 = f0;

    // @formatter:off
    public static final RegisterArray simdRegisters = new RegisterArray(
        f0,  f1,  f2,  f3,  f4,  f5,  f6,  f7,
        f8,  f9,  f10, f11, f12, f13, f14, f15,
        f16, f17, f18, f19, f20, f21, f22, f23,
        f24, f25, f26, f27, f28, f29, f30, f31
    );
    // @formatter:on

    // @formatter:off
    public static final RegisterArray allRegisters = new RegisterArray(
        zero, ra,  tp,  sp,  a0,  a1,  a2,  a3,
        a4,   a5,  a6,  a7,  t0,  t1,  t2,  t3,
        t4,   t5,  t6,  t7,  t8,  rx,  fp,  s0,
        s1,   s2,  s3,  s4,  s5,  s6,  s7,  s8,

        f0,   f1,  f2,  f3,  f4,  f5,  f6,  f7,
        f8,   f9,  f10, f11, f12, f13, f14, f15,
        f16,  f17, f18, f19, f20, f21, f22, f23,
        f24,  f25, f26, f27, f28, f29, f30, f31
    );
    // @formatter:on

    /**
     * Basic set of CPU features mirroring what is returned from the cpuid instruction. See:
     * {@code VM_Version::cpuFeatureFlags}.
     */
    public enum CPUFeature {
        LA32,
        LA64,
        LLEXC,
        SCDLY,
        LLDBAR,
        LBT_X86,
        LBT_ARM,
        LBT_MIPS,
        CCDMA,
        COMPLEX,
        FP,
        CRYPTO,
        LSX,
        LASX,
        LAM,
        LLSYNC,
        TGTSYNC,
        ULSYNC,
        UAL
    }

    private final EnumSet<CPUFeature> features;

    /**
     * Set of flags to control code emission.
     */
    public enum Flag {
        useLSX,
        useLASX
    }

    private final EnumSet<Flag> flags;

    public LoongArch64(EnumSet<CPUFeature> features, EnumSet<Flag> flags) {
        super("loongarch64", LoongArch64Kind.QWORD, ByteOrder.LITTLE_ENDIAN, true, allRegisters, 0, 0, 0);
        this.features = features;
        this.flags = flags;
    }

    public EnumSet<CPUFeature> getFeatures() {
        return features;
    }

    public EnumSet<Flag> getFlags() {
        return flags;
    }

    @Override
    public PlatformKind getPlatformKind(JavaKind javaKind) {
        switch (javaKind) {
            case Boolean:
            case Byte:
                return LoongArch64Kind.BYTE;
            case Short:
            case Char:
                return LoongArch64Kind.WORD;
            case Int:
                return LoongArch64Kind.DWORD;
            case Long:
            case Object:
                return LoongArch64Kind.QWORD;
            case Float:
                return LoongArch64Kind.SINGLE;
            case Double:
                return LoongArch64Kind.DOUBLE;
            default:
                return null;
        }
    }

    @Override
    public boolean canStoreValue(RegisterCategory category, PlatformKind platformKind) {
        LoongArch64Kind kind = (LoongArch64Kind) platformKind;
        if (kind.isInteger()) {
            return category.equals(CPU);
        } else if (kind.isSIMD()) {
            return category.equals(SIMD);
        }
        return false;
    }

    @Override
    public LoongArch64Kind getLargestStorableKind(RegisterCategory category) {
        if (category.equals(CPU)) {
            return LoongArch64Kind.QWORD;
        } else if (category.equals(SIMD)) {
            return LoongArch64Kind.V256_QWORD;
        } else {
            return null;
        }
    }
}
