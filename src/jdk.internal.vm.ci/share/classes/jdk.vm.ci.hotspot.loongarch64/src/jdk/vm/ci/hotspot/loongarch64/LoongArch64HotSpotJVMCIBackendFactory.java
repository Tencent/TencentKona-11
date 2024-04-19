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
package jdk.vm.ci.hotspot.loongarch64;

import static java.util.Collections.emptyMap;
import static jdk.vm.ci.common.InitTimer.timer;

import java.util.EnumSet;
import java.util.Map;

import jdk.vm.ci.loongarch64.LoongArch64;
import jdk.vm.ci.loongarch64.LoongArch64.CPUFeature;
import jdk.vm.ci.code.Architecture;
import jdk.vm.ci.code.RegisterConfig;
import jdk.vm.ci.code.TargetDescription;
import jdk.vm.ci.code.stack.StackIntrospection;
import jdk.vm.ci.common.InitTimer;
import jdk.vm.ci.hotspot.HotSpotCodeCacheProvider;
import jdk.vm.ci.hotspot.HotSpotConstantReflectionProvider;
import jdk.vm.ci.hotspot.HotSpotJVMCIBackendFactory;
import jdk.vm.ci.hotspot.HotSpotJVMCIRuntime;
import jdk.vm.ci.hotspot.HotSpotMetaAccessProvider;
import jdk.vm.ci.hotspot.HotSpotStackIntrospection;
import jdk.vm.ci.meta.ConstantReflectionProvider;
import jdk.vm.ci.runtime.JVMCIBackend;

public class LoongArch64HotSpotJVMCIBackendFactory implements HotSpotJVMCIBackendFactory {

    protected EnumSet<LoongArch64.CPUFeature> computeFeatures(@SuppressWarnings("unused") LoongArch64HotSpotVMConfig config) {
        // Configure the feature set using the HotSpot flag settings.
        EnumSet<LoongArch64.CPUFeature> features = EnumSet.noneOf(LoongArch64.CPUFeature.class);

        if ((config.vmVersionFeatures & config.loongarch64LA32) != 0) {
            features.add(LoongArch64.CPUFeature.LA32);
        }

        if ((config.vmVersionFeatures & config.loongarch64LA64) != 0) {
            features.add(LoongArch64.CPUFeature.LA64);
        }

        if ((config.vmVersionFeatures & config.loongarch64LLEXC) != 0) {
            features.add(LoongArch64.CPUFeature.LLEXC);
        }

        if ((config.vmVersionFeatures & config.loongarch64SCDLY) != 0) {
            features.add(LoongArch64.CPUFeature.SCDLY);
        }

        if ((config.vmVersionFeatures & config.loongarch64LLDBAR) != 0) {
            features.add(LoongArch64.CPUFeature.LLDBAR);
        }

        if ((config.vmVersionFeatures & config.loongarch64LBT_X86) != 0) {
            features.add(LoongArch64.CPUFeature.LBT_X86);
        }

        if ((config.vmVersionFeatures & config.loongarch64LBT_ARM) != 0) {
            features.add(LoongArch64.CPUFeature.LBT_ARM);
        }

        if ((config.vmVersionFeatures & config.loongarch64LBT_MIPS) != 0) {
            features.add(LoongArch64.CPUFeature.LBT_MIPS);
        }

        if ((config.vmVersionFeatures & config.loongarch64CCDMA) != 0) {
            features.add(LoongArch64.CPUFeature.CCDMA);
        }

        if ((config.vmVersionFeatures & config.loongarch64COMPLEX) != 0) {
            features.add(LoongArch64.CPUFeature.COMPLEX);
        }

        if ((config.vmVersionFeatures & config.loongarch64FP) != 0) {
            features.add(LoongArch64.CPUFeature.FP);
        }

        if ((config.vmVersionFeatures & config.loongarch64CRYPTO) != 0) {
            features.add(LoongArch64.CPUFeature.CRYPTO);
        }

        if ((config.vmVersionFeatures & config.loongarch64LSX) != 0) {
            features.add(LoongArch64.CPUFeature.LSX);
        }

        if ((config.vmVersionFeatures & config.loongarch64LASX) != 0) {
            features.add(LoongArch64.CPUFeature.LASX);
        }

        if ((config.vmVersionFeatures & config.loongarch64LAM) != 0) {
            features.add(LoongArch64.CPUFeature.LAM);
        }

        if ((config.vmVersionFeatures & config.loongarch64LLSYNC) != 0) {
            features.add(LoongArch64.CPUFeature.LLSYNC);
        }

        if ((config.vmVersionFeatures & config.loongarch64TGTSYNC) != 0) {
            features.add(LoongArch64.CPUFeature.TGTSYNC);
        }

        if ((config.vmVersionFeatures & config.loongarch64ULSYNC) != 0) {
            features.add(LoongArch64.CPUFeature.ULSYNC);
        }

        if ((config.vmVersionFeatures & config.loongarch64UAL) != 0) {
            features.add(LoongArch64.CPUFeature.UAL);
        }

        return features;
    }

    protected EnumSet<LoongArch64.Flag> computeFlags(@SuppressWarnings("unused") LoongArch64HotSpotVMConfig config) {
        EnumSet<LoongArch64.Flag> flags = EnumSet.noneOf(LoongArch64.Flag.class);

        if (config.useLSX) {
            flags.add(LoongArch64.Flag.useLSX);
        }

        if (config.useLASX) {
            flags.add(LoongArch64.Flag.useLASX);
        }

        return flags;
    }

    protected TargetDescription createTarget(LoongArch64HotSpotVMConfig config) {
        final int stackFrameAlignment = 16;
        final int implicitNullCheckLimit = 4096;
        final boolean inlineObjects = true;
        Architecture arch = new LoongArch64(computeFeatures(config), computeFlags(config));
        return new TargetDescription(arch, true, stackFrameAlignment, implicitNullCheckLimit, inlineObjects);
    }

    protected HotSpotConstantReflectionProvider createConstantReflection(HotSpotJVMCIRuntime runtime) {
        return new HotSpotConstantReflectionProvider(runtime);
    }

    protected RegisterConfig createRegisterConfig(LoongArch64HotSpotVMConfig config, TargetDescription target) {
        return new LoongArch64HotSpotRegisterConfig(target, config.useCompressedOops);
    }

    protected HotSpotCodeCacheProvider createCodeCache(HotSpotJVMCIRuntime runtime, TargetDescription target, RegisterConfig regConfig) {
        return new HotSpotCodeCacheProvider(runtime, runtime.getConfig(), target, regConfig);
    }

    protected HotSpotMetaAccessProvider createMetaAccess(HotSpotJVMCIRuntime runtime) {
        return new HotSpotMetaAccessProvider(runtime);
    }

    @Override
    public String getArchitecture() {
        return "loongarch64";
    }

    @Override
    public String toString() {
        return "JVMCIBackend:" + getArchitecture();
    }

    @Override
    @SuppressWarnings("try")
    public JVMCIBackend createJVMCIBackend(HotSpotJVMCIRuntime runtime, JVMCIBackend host) {

        assert host == null;
        LoongArch64HotSpotVMConfig config = new LoongArch64HotSpotVMConfig(runtime.getConfigStore());
        TargetDescription target = createTarget(config);

        RegisterConfig regConfig;
        HotSpotCodeCacheProvider codeCache;
        ConstantReflectionProvider constantReflection;
        HotSpotMetaAccessProvider metaAccess;
        StackIntrospection stackIntrospection;
        try (InitTimer t = timer("create providers")) {
            try (InitTimer rt = timer("create MetaAccess provider")) {
                metaAccess = createMetaAccess(runtime);
            }
            try (InitTimer rt = timer("create RegisterConfig")) {
                regConfig = createRegisterConfig(config, target);
            }
            try (InitTimer rt = timer("create CodeCache provider")) {
                codeCache = createCodeCache(runtime, target, regConfig);
            }
            try (InitTimer rt = timer("create ConstantReflection provider")) {
                constantReflection = createConstantReflection(runtime);
            }
            try (InitTimer rt = timer("create StackIntrospection provider")) {
                stackIntrospection = new HotSpotStackIntrospection(runtime);
            }
        }
        try (InitTimer rt = timer("instantiate backend")) {
            return createBackend(metaAccess, codeCache, constantReflection, stackIntrospection);
        }
    }

    protected JVMCIBackend createBackend(HotSpotMetaAccessProvider metaAccess, HotSpotCodeCacheProvider codeCache, ConstantReflectionProvider constantReflection,
                    StackIntrospection stackIntrospection) {
        return new JVMCIBackend(metaAccess, codeCache, constantReflection, stackIntrospection);
    }
}
