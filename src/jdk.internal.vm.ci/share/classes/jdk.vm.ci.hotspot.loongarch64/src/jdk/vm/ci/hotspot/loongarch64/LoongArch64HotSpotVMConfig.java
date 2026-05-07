/*
 * Copyright (c) 2016, 2022, Oracle and/or its affiliates. All rights reserved.
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
package jdk.vm.ci.hotspot.loongarch64;

import jdk.vm.ci.hotspot.HotSpotVMConfigAccess;
import jdk.vm.ci.hotspot.HotSpotVMConfigStore;
import jdk.vm.ci.services.Services;

/**
 * Used to access native configuration details.
 *
 * All non-static, public fields in this class are so that they can be compiled as constants.
 */
class LoongArch64HotSpotVMConfig extends HotSpotVMConfigAccess {

    LoongArch64HotSpotVMConfig(HotSpotVMConfigStore config) {
        super(config);
    }

    final boolean useCompressedOops = getFlag("UseCompressedOops", Boolean.class);

    // CPU Capabilities

    /*
     * These flags are set based on the corresponding command line flags.
     */
    final boolean useLSX = getFlag("UseLSX", Boolean.class);
    final boolean useLASX = getFlag("UseLASX", Boolean.class);

    final long vmVersionFeatures = getFieldValue("Abstract_VM_Version::_features", Long.class, "uint64_t");

    /*
     * These flags are set if the corresponding support is in the hardware.
     */
    // Checkstyle: stop
    final long loongarch64LA32 = getConstant("VM_Version::CPU_LA32", Long.class);
    final long loongarch64LA64 = getConstant("VM_Version::CPU_LA64", Long.class);
    final long loongarch64LLEXC = getConstant("VM_Version::CPU_LLEXC", Long.class);
    final long loongarch64SCDLY = getConstant("VM_Version::CPU_SCDLY", Long.class);
    final long loongarch64LLDBAR = getConstant("VM_Version::CPU_LLDBAR", Long.class);
    final long loongarch64LBT_X86 = getConstant("VM_Version::CPU_LBT_X86", Long.class);
    final long loongarch64LBT_ARM = getConstant("VM_Version::CPU_LBT_ARM", Long.class);
    final long loongarch64LBT_MIPS = getConstant("VM_Version::CPU_LBT_MIPS", Long.class);
    final long loongarch64CCDMA = getConstant("VM_Version::CPU_CCDMA", Long.class);
    final long loongarch64COMPLEX = getConstant("VM_Version::CPU_COMPLEX", Long.class);
    final long loongarch64FP = getConstant("VM_Version::CPU_FP", Long.class);
    final long loongarch64CRYPTO = getConstant("VM_Version::CPU_CRYPTO", Long.class);
    final long loongarch64LSX = getConstant("VM_Version::CPU_LSX", Long.class);
    final long loongarch64LASX = getConstant("VM_Version::CPU_LASX", Long.class);
    final long loongarch64LAM = getConstant("VM_Version::CPU_LAM", Long.class);
    final long loongarch64LLSYNC = getConstant("VM_Version::CPU_LLSYNC", Long.class);
    final long loongarch64TGTSYNC = getConstant("VM_Version::CPU_TGTSYNC", Long.class);
    final long loongarch64ULSYNC = getConstant("VM_Version::CPU_ULSYNC", Long.class);
    final long loongarch64UAL = getConstant("VM_Version::CPU_UAL", Long.class);
    // Checkstyle: resume
}
