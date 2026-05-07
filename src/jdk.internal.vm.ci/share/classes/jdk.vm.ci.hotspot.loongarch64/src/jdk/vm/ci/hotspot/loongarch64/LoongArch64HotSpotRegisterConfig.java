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

import static jdk.vm.ci.loongarch64.LoongArch64.ra;
import static jdk.vm.ci.loongarch64.LoongArch64.a0;
import static jdk.vm.ci.loongarch64.LoongArch64.a1;
import static jdk.vm.ci.loongarch64.LoongArch64.a2;
import static jdk.vm.ci.loongarch64.LoongArch64.a3;
import static jdk.vm.ci.loongarch64.LoongArch64.a4;
import static jdk.vm.ci.loongarch64.LoongArch64.a5;
import static jdk.vm.ci.loongarch64.LoongArch64.a6;
import static jdk.vm.ci.loongarch64.LoongArch64.a7;
import static jdk.vm.ci.loongarch64.LoongArch64.SCR1;
import static jdk.vm.ci.loongarch64.LoongArch64.SCR2;
import static jdk.vm.ci.loongarch64.LoongArch64.t0;
import static jdk.vm.ci.loongarch64.LoongArch64.v0;
import static jdk.vm.ci.loongarch64.LoongArch64.s5;
import static jdk.vm.ci.loongarch64.LoongArch64.s6;
import static jdk.vm.ci.loongarch64.LoongArch64.sp;
import static jdk.vm.ci.loongarch64.LoongArch64.fp;
import static jdk.vm.ci.loongarch64.LoongArch64.tp;
import static jdk.vm.ci.loongarch64.LoongArch64.rx;
import static jdk.vm.ci.loongarch64.LoongArch64.f0;
import static jdk.vm.ci.loongarch64.LoongArch64.f1;
import static jdk.vm.ci.loongarch64.LoongArch64.f2;
import static jdk.vm.ci.loongarch64.LoongArch64.f3;
import static jdk.vm.ci.loongarch64.LoongArch64.f4;
import static jdk.vm.ci.loongarch64.LoongArch64.f5;
import static jdk.vm.ci.loongarch64.LoongArch64.f6;
import static jdk.vm.ci.loongarch64.LoongArch64.f7;
import static jdk.vm.ci.loongarch64.LoongArch64.fv0;
import static jdk.vm.ci.loongarch64.LoongArch64.zero;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

import jdk.vm.ci.loongarch64.LoongArch64;
import jdk.vm.ci.code.Architecture;
import jdk.vm.ci.code.CallingConvention;
import jdk.vm.ci.code.CallingConvention.Type;
import jdk.vm.ci.code.Register;
import jdk.vm.ci.code.RegisterArray;
import jdk.vm.ci.code.RegisterAttributes;
import jdk.vm.ci.code.RegisterConfig;
import jdk.vm.ci.code.StackSlot;
import jdk.vm.ci.code.TargetDescription;
import jdk.vm.ci.code.ValueKindFactory;
import jdk.vm.ci.common.JVMCIError;
import jdk.vm.ci.hotspot.HotSpotCallingConventionType;
import jdk.vm.ci.meta.AllocatableValue;
import jdk.vm.ci.meta.JavaKind;
import jdk.vm.ci.meta.JavaType;
import jdk.vm.ci.meta.PlatformKind;
import jdk.vm.ci.meta.Value;
import jdk.vm.ci.meta.ValueKind;

public class LoongArch64HotSpotRegisterConfig implements RegisterConfig {

    private final TargetDescription target;

    private final RegisterArray allocatable;

    /**
     * The caller saved registers always include all parameter registers.
     */
    private final RegisterArray callerSaved;

    private final boolean allAllocatableAreCallerSaved;

    private final RegisterAttributes[] attributesMap;

    @Override
    public RegisterArray getAllocatableRegisters() {
        return allocatable;
    }

    @Override
    public RegisterArray filterAllocatableRegisters(PlatformKind kind, RegisterArray registers) {
        ArrayList<Register> list = new ArrayList<>();
        for (Register reg : registers) {
            if (target.arch.canStoreValue(reg.getRegisterCategory(), kind)) {
                list.add(reg);
            }
        }

        return new RegisterArray(list);
    }

    @Override
    public RegisterAttributes[] getAttributesMap() {
        return attributesMap.clone();
    }

    private final RegisterArray javaGeneralParameterRegisters = new RegisterArray(t0, a0, a1, a2, a3, a4, a5, a6, a7);
    private final RegisterArray nativeGeneralParameterRegisters = new RegisterArray(a0, a1, a2, a3, a4, a5, a6, a7);
    private final RegisterArray floatParameterRegisters = new RegisterArray(f0, f1, f2, f3, f4, f5, f6, f7);

    public static final Register heapBaseRegister = s5;
    public static final Register TREG = s6;

    private static final RegisterArray reservedRegisters = new RegisterArray(fp, ra, zero, sp, tp, rx, SCR1, SCR2, TREG);

    private static RegisterArray initAllocatable(Architecture arch, boolean reserveForHeapBase) {
        RegisterArray allRegisters = arch.getAvailableValueRegisters();
        Register[] registers = new Register[allRegisters.size() - reservedRegisters.size() - (reserveForHeapBase ? 1 : 0)];
        List<Register> reservedRegistersList = reservedRegisters.asList();

        int idx = 0;
        for (Register reg : allRegisters) {
            if (reservedRegistersList.contains(reg)) {
                // skip reserved registers
                continue;
            }
            if (reserveForHeapBase && reg.equals(heapBaseRegister)) {
                // skip heap base register
                continue;
            }

            registers[idx++] = reg;
        }

        assert idx == registers.length;
        return new RegisterArray(registers);
    }

    public LoongArch64HotSpotRegisterConfig(TargetDescription target, boolean useCompressedOops) {
        this(target, initAllocatable(target.arch, useCompressedOops));
        assert callerSaved.size() >= allocatable.size();
    }

    public LoongArch64HotSpotRegisterConfig(TargetDescription target, RegisterArray allocatable) {
        this.target = target;

        this.allocatable = allocatable;
        Set<Register> callerSaveSet = new HashSet<>();
        allocatable.addTo(callerSaveSet);
        floatParameterRegisters.addTo(callerSaveSet);
        javaGeneralParameterRegisters.addTo(callerSaveSet);
        nativeGeneralParameterRegisters.addTo(callerSaveSet);
        callerSaved = new RegisterArray(callerSaveSet);

        allAllocatableAreCallerSaved = true;
        attributesMap = RegisterAttributes.createMap(this, LoongArch64.allRegisters);
    }

    @Override
    public RegisterArray getCallerSaveRegisters() {
        return callerSaved;
    }

    @Override
    public RegisterArray getCalleeSaveRegisters() {
        return null;
    }

    @Override
    public boolean areAllAllocatableRegistersCallerSaved() {
        return allAllocatableAreCallerSaved;
    }

    @Override
    public CallingConvention getCallingConvention(Type type, JavaType returnType, JavaType[] parameterTypes, ValueKindFactory<?> valueKindFactory) {
        HotSpotCallingConventionType hotspotType = (HotSpotCallingConventionType) type;
        if (type == HotSpotCallingConventionType.NativeCall) {
            return callingConvention(nativeGeneralParameterRegisters, returnType, parameterTypes, hotspotType, valueKindFactory);
        }
        // On x64, parameter locations are the same whether viewed
        // from the caller or callee perspective
        return callingConvention(javaGeneralParameterRegisters, returnType, parameterTypes, hotspotType, valueKindFactory);
    }

    @Override
    public RegisterArray getCallingConventionRegisters(Type type, JavaKind kind) {
        HotSpotCallingConventionType hotspotType = (HotSpotCallingConventionType) type;
        switch (kind) {
            case Boolean:
            case Byte:
            case Short:
            case Char:
            case Int:
            case Long:
            case Object:
                return hotspotType == HotSpotCallingConventionType.NativeCall ? nativeGeneralParameterRegisters : javaGeneralParameterRegisters;
            case Float:
            case Double:
                return floatParameterRegisters;
            default:
                throw JVMCIError.shouldNotReachHere();
        }
    }

    private CallingConvention callingConvention(RegisterArray generalParameterRegisters, JavaType returnType, JavaType[] parameterTypes, HotSpotCallingConventionType type,
                    ValueKindFactory<?> valueKindFactory) {
        AllocatableValue[] locations = new AllocatableValue[parameterTypes.length];

        int currentGeneral = 0;
        int currentFloat = 0;
        int currentStackOffset = 0;

        for (int i = 0; i < parameterTypes.length; i++) {
            final JavaKind kind = parameterTypes[i].getJavaKind().getStackKind();

            switch (kind) {
                case Byte:
                case Boolean:
                case Short:
                case Char:
                case Int:
                case Long:
                case Object:
                    if (currentGeneral < generalParameterRegisters.size()) {
                        Register register = generalParameterRegisters.get(currentGeneral++);
                        locations[i] = register.asValue(valueKindFactory.getValueKind(kind));
                    }
                    break;
                case Float:
                case Double:
                    if (currentFloat < floatParameterRegisters.size()) {
                        Register register = floatParameterRegisters.get(currentFloat++);
                        locations[i] = register.asValue(valueKindFactory.getValueKind(kind));
                    } else if (currentGeneral < generalParameterRegisters.size()) {
                        Register register = generalParameterRegisters.get(currentGeneral++);
                        locations[i] = register.asValue(valueKindFactory.getValueKind(kind));
                    }
                    break;
                default:
                    throw JVMCIError.shouldNotReachHere();
            }

            if (locations[i] == null) {
                ValueKind<?> valueKind = valueKindFactory.getValueKind(kind);
                locations[i] = StackSlot.get(valueKind, currentStackOffset, !type.out);
                currentStackOffset += Math.max(valueKind.getPlatformKind().getSizeInBytes(), target.wordSize);
            }
        }

        JavaKind returnKind = returnType == null ? JavaKind.Void : returnType.getJavaKind();
        AllocatableValue returnLocation = returnKind == JavaKind.Void ? Value.ILLEGAL : getReturnRegister(returnKind).asValue(valueKindFactory.getValueKind(returnKind.getStackKind()));
        return new CallingConvention(currentStackOffset, returnLocation, locations);
    }

    @Override
    public Register getReturnRegister(JavaKind kind) {
        switch (kind) {
            case Boolean:
            case Byte:
            case Char:
            case Short:
            case Int:
            case Long:
            case Object:
                return v0;
            case Float:
            case Double:
                return fv0;
            case Void:
            case Illegal:
                return null;
            default:
                throw new UnsupportedOperationException("no return register for type " + kind);
        }
    }

    @Override
    public Register getFrameRegister() {
        return sp;
    }

    @Override
    public String toString() {
        return String.format("Allocatable: " + getAllocatableRegisters() + "%n" + "CallerSave:  " + getCallerSaveRegisters() + "%n");
    }
}
