/*
 * Copyright (c) 2020, 2022, Oracle and/or its affiliates. All rights reserved.
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

package jdk.vm.ci.code.test.loongarch64;

import jdk.vm.ci.loongarch64.LoongArch64;
import jdk.vm.ci.loongarch64.LoongArch64Kind;
import jdk.vm.ci.code.CallingConvention;
import jdk.vm.ci.code.CodeCacheProvider;
import jdk.vm.ci.code.DebugInfo;
import jdk.vm.ci.code.Register;
import jdk.vm.ci.code.RegisterArray;
import jdk.vm.ci.code.RegisterValue;
import jdk.vm.ci.code.StackSlot;
import jdk.vm.ci.code.site.ConstantReference;
import jdk.vm.ci.code.site.DataSectionReference;
import jdk.vm.ci.code.test.TestAssembler;
import jdk.vm.ci.code.test.TestHotSpotVMConfig;
import jdk.vm.ci.hotspot.HotSpotCallingConventionType;
import jdk.vm.ci.hotspot.HotSpotConstant;
import jdk.vm.ci.hotspot.HotSpotForeignCallTarget;
import jdk.vm.ci.meta.AllocatableValue;
import jdk.vm.ci.meta.JavaKind;
import jdk.vm.ci.meta.VMConstant;

public class LoongArch64TestAssembler extends TestAssembler {

    private static final Register scratchRegister = LoongArch64.SCR1;
    private static final Register doubleScratch = LoongArch64.f23;
    private static final RegisterArray nativeGeneralParameterRegisters = new RegisterArray(LoongArch64.a0,
                                                                      LoongArch64.a1, LoongArch64.a2,
                                                                      LoongArch64.a3, LoongArch64.a4,
                                                                      LoongArch64.a5, LoongArch64.a6,
                                                                      LoongArch64.a7);
    private static final RegisterArray floatParameterRegisters = new RegisterArray(LoongArch64.f0,
                                                                      LoongArch64.f1, LoongArch64.f2,
                                                                      LoongArch64.f3, LoongArch64.f4,
                                                                      LoongArch64.f5, LoongArch64.f6,
                                                                      LoongArch64.f7);
    private static int currentGeneral = 0;
    private static int currentFloat = 0;
    public LoongArch64TestAssembler(CodeCacheProvider codeCache, TestHotSpotVMConfig config) {
        super(codeCache, config,
              16 /* initialFrameSize */, 16 /* stackAlignment */,
              LoongArch64Kind.UDWORD /* narrowOopKind */,
              /* registers */
              LoongArch64.a0, LoongArch64.a1, LoongArch64.a2, LoongArch64.a3,
              LoongArch64.a4, LoongArch64.a5, LoongArch64.a6, LoongArch64.a7);
    }

    private static int low(int x, int l) {
        assert l < 32;
        return (x >> 0) & ((1 << l)-1);
    }

    private static int low16(int x) {
        return low(x, 16);
    }

    private void emitNop() {
        code.emitInt(0x3400000);
    }

    private void emitPcaddu12i(Register rj, int si20) {
        // pcaddu12i
        code.emitInt((0b0001110 << 25)
                     | (low(si20, 20) << 5)
                     | rj.encoding);
    }

    private void emitAdd(Register rd, Register rj, Register rk) {
        // add_d
        code.emitInt((0b00000000000100001 << 15)
                     | (rk.encoding << 10)
                     | (rj.encoding << 5)
                     | rd.encoding);
    }

    private void emitAdd(Register rd, Register rj, int si12) {
        // addi_d
        code.emitInt((0b0000001011 << 22)
                     | (low(si12, 12) << 10)
                     | (rj.encoding << 5)
                     | rd.encoding);
    }

    private void emitSub(Register rd, Register rj, Register rk) {
        // sub_d
        code.emitInt((0b00000000000100011 << 15)
                     | (rk.encoding << 10)
                     | (rj.encoding << 5)
                     | rd.encoding);
    }

    private void emitShiftLeft(Register rd, Register rj, int shift) {
        // slli_d
        code.emitInt((0b00000000010000 << 18)
                     | (low(( (0b01  << 6) | shift ), 8) << 10)
                     | (rj.encoding << 5)
                     | rd.encoding);
    }

    private void emitLu12i_w(Register rj, int imm20) {
        // lu12i_w
        code.emitInt((0b0001010 << 25)
                     | (low(imm20, 20)<<5)
                     | rj.encoding);
    }

    private void emitOri(Register rd, Register rj, int ui12) {
        // ori
        code.emitInt((0b0000001110 << 22)
                     | (low(ui12, 12) << 10)
                     | (rj.encoding << 5)
                     | rd.encoding);
    }

    private void emitLu32i_d(Register rj, int imm20) {
         // lu32i_d
        code.emitInt((0b0001011 << 25)
                     | (low(imm20, 20)<<5)
                     | rj.encoding);
    }

    private void emitLu52i_d(Register rd, Register rj, int imm12) {
        // lu52i_d
        code.emitInt((0b0000001100 << 22)
                     | (low(imm12, 12) << 10)
                     | (rj.encoding << 5)
                     | rd.encoding);
    }

    private void emitLoadImmediate(Register rd, int imm32) {
        emitLu12i_w(rd, (imm32 >> 12) & 0xfffff);
        emitOri(rd, rd, imm32 & 0xfff);
    }

    private void emitLi52(Register rj, long imm) {
        emitLu12i_w(rj, (int) ((imm >> 12) & 0xfffff));
        emitOri(rj, rj, (int) (imm & 0xfff));
        emitLu32i_d(rj, (int) ((imm >> 32) & 0xfffff));
    }

    private void emitLi64(Register rj, long imm) {
        emitLu12i_w(rj, (int) ((imm >> 12) & 0xfffff));
        emitOri(rj, rj, (int) (imm & 0xfff));
        emitLu32i_d(rj, (int) ((imm >> 32) & 0xfffff));
        emitLu52i_d(rj, rj, (int) ((imm >> 52) & 0xfff));
    }

    private void emitOr(Register rd, Register rj, Register rk) {
        // orr
        code.emitInt((0b00000000000101010 << 15)
                     | (rk.encoding << 10)
                     | (rj.encoding << 5)
                     | rd.encoding);
    }

    private void emitMove(Register rd, Register rs) {
        // move
        emitOr(rd, rs, LoongArch64.zero);
    }

    private void emitMovfr2gr(Register rd, LoongArch64Kind kind, Register rj) {
        // movfr2gr_s/movfr2gr_d
        int opc = 0;
        switch (kind) {
            case SINGLE: opc = 0b0000000100010100101101; break;
            case DOUBLE: opc = 0b0000000100010100101110; break;
            default: throw new IllegalArgumentException();
        }
        code.emitInt((opc << 10)
                     | (rj.encoding << 5)
                     | rd.encoding);
    }

    private void emitLoadRegister(Register rd, LoongArch64Kind kind, Register rj, int offset) {
        // load
        assert offset >= 0;
        int opc = 0;
        switch (kind) {
            case BYTE:   opc = 0b0010100000; break;
            case WORD:   opc = 0b0010100001; break;
            case DWORD:  opc = 0b0010100010; break;
            case QWORD:  opc = 0b0010100011; break;
            case UDWORD: opc = 0b0010101010; break;
            case SINGLE: opc = 0b0010101100; break;
            case DOUBLE: opc = 0b0010101110; break;
            default: throw new IllegalArgumentException();
        }
        code.emitInt((opc << 22)
                     | (low(offset, 12) << 10)
                     | (rj.encoding << 5)
                     | rd.encoding);
    }

    private void emitStoreRegister(Register rd, LoongArch64Kind kind, Register rj, int offset) {
        // store
        assert offset >= 0;
        int opc = 0;
        switch (kind) {
            case BYTE:   opc = 0b0010100100; break;
            case WORD:   opc = 0b0010100101; break;
            case DWORD:  opc = 0b0010100110; break;
            case QWORD:  opc = 0b0010100111; break;
            case SINGLE: opc = 0b0010101101; break;
            case DOUBLE: opc = 0b0010101111; break;
            default: throw new IllegalArgumentException();
        }
        code.emitInt((opc << 22)
                     | (low(offset, 12) << 10)
                     | (rj.encoding << 5)
                     | rd.encoding);
    }

    private void emitJirl(Register rd, Register rj, int offs) {
        // jirl
        code.emitInt((0b010011 << 26)
                     | (low16(offs >> 2) << 10)
                     | (rj.encoding << 5)
                     | rd.encoding);
    }

    @Override
    public void emitGrowStack(int size) {
        assert size % 16 == 0;
        if (size > -4096 && size < 0) {
            emitAdd(LoongArch64.sp, LoongArch64.sp, -size);
        } else if (size == 0) {
            // No-op
        } else if (size < 4096) {
            emitAdd(LoongArch64.sp, LoongArch64.sp, -size);
        } else if (size < 65535) {
            emitLoadImmediate(scratchRegister, size);
            emitSub(LoongArch64.sp, LoongArch64.sp, scratchRegister);
        } else {
            throw new IllegalArgumentException();
        }
    }

    @Override
    public void emitPrologue() {
        // Must be patchable by NativeJump::patch_verified_entry
        emitNop();
        emitGrowStack(32);
        emitStoreRegister(LoongArch64.ra, LoongArch64Kind.QWORD, LoongArch64.sp, 24);
        emitStoreRegister(LoongArch64.fp, LoongArch64Kind.QWORD, LoongArch64.sp, 16);
        emitGrowStack(-16);
        emitMove(LoongArch64.fp, LoongArch64.sp);
        setDeoptRescueSlot(newStackSlot(LoongArch64Kind.QWORD));
    }

    @Override
    public void emitEpilogue() {
        recordMark(config.MARKID_DEOPT_HANDLER_ENTRY);
        recordCall(new HotSpotForeignCallTarget(config.handleDeoptStub), 4*4, true, null);
        emitCall(0xdeaddeaddeadL);
    }

    @Override
    public void emitCallPrologue(CallingConvention cc, Object... prim) {
        emitGrowStack(cc.getStackSize());
        frameSize += cc.getStackSize();
        AllocatableValue[] args = cc.getArguments();
        for (int i = 0; i < args.length; i++) {
            emitLoad(args[i], prim[i]);
        }
        currentGeneral = 0;
        currentFloat = 0;
    }

    @Override
    public void emitCallEpilogue(CallingConvention cc) {
        emitGrowStack(-cc.getStackSize());
        frameSize -= cc.getStackSize();
    }

    @Override
    public void emitCall(long addr) {
        // long call (absolute)
        // lu12i_w(T4, split_low20(value >> 12));
        // lu32i_d(T4, split_low20(value >> 32));
        // jirl(RA, T4, split_low12(value));
        emitLu12i_w(LoongArch64.t4, (int) ((addr >> 12) & 0xfffff));
        emitLu32i_d(LoongArch64.t4, (int) ((addr >> 32) & 0xfffff));
        emitJirl(LoongArch64.ra, LoongArch64.t4, (int) (addr & 0xfff));
    }

    @Override
    public void emitLoad(AllocatableValue av, Object prim) {
        if (av instanceof RegisterValue) {
            Register reg = ((RegisterValue) av).getRegister();
            if (prim instanceof Float) {
                if (currentFloat < floatParameterRegisters.size()) {
                  currentFloat++;
                  emitLoadFloat(reg, (Float) prim);
                } else if (currentGeneral < nativeGeneralParameterRegisters.size()) {
                  currentGeneral++;
                  emitLoadFloat(doubleScratch, (Float) prim);
                  emitMovfr2gr(reg, LoongArch64Kind.SINGLE, doubleScratch);
                }
            } else if (prim instanceof Double) {
                if (currentFloat < floatParameterRegisters.size()) {
                  currentFloat++;
                  emitLoadDouble(reg, (Double) prim);
                } else if (currentGeneral < nativeGeneralParameterRegisters.size()) {
                  currentGeneral++;
                  emitLoadDouble(doubleScratch, (Double) prim);
                  emitMovfr2gr(reg, LoongArch64Kind.DOUBLE, doubleScratch);
                }
            } else if (prim instanceof Integer) {
                emitLoadInt(reg, (Integer) prim);
            } else if (prim instanceof Long) {
                emitLoadLong(reg, (Long) prim);
            }
        } else if (av instanceof StackSlot) {
            StackSlot slot = (StackSlot) av;
            if (prim instanceof Float) {
                emitFloatToStack(slot, emitLoadFloat(doubleScratch, (Float) prim));
            } else if (prim instanceof Double) {
                emitDoubleToStack(slot, emitLoadDouble(doubleScratch, (Double) prim));
            } else if (prim instanceof Integer) {
                emitIntToStack(slot, emitLoadInt(scratchRegister, (Integer) prim));
            } else if (prim instanceof Long) {
                emitLongToStack(slot, emitLoadLong(scratchRegister, (Long) prim));
            } else {
                assert false : "Unimplemented";
            }
        } else {
            throw new IllegalArgumentException("Unknown value " + av);
        }
    }

    @Override
    public Register emitLoadPointer(HotSpotConstant c) {
        recordDataPatchInCode(new ConstantReference((VMConstant) c));

        Register ret = newRegister();
        // need to match patchable_li52 instruction sequence
        // lu12i_ori_lu32i
        emitLi52(ret, 0xdeaddead);
        return ret;
    }

    @Override
    public Register emitLoadPointer(Register b, int offset) {
        Register ret = newRegister();
        emitLoadRegister(ret, LoongArch64Kind.QWORD, b, offset);
        return ret;
    }

    @Override
    public Register emitLoadNarrowPointer(DataSectionReference ref) {
        recordDataPatchInCode(ref);

        Register ret = newRegister();
        emitPcaddu12i(ret, 0xdead >> 12);
        emitAdd(ret, ret, 0xdead & 0xfff);
        emitLoadRegister(ret, LoongArch64Kind.UDWORD, ret, 0);
        return ret;
    }

    @Override
    public Register emitLoadPointer(DataSectionReference ref) {
        recordDataPatchInCode(ref);

        Register ret = newRegister();
        emitPcaddu12i(ret, 0xdead >> 12);
        emitAdd(ret, ret, 0xdead & 0xfff);
        emitLoadRegister(ret, LoongArch64Kind.QWORD, ret, 0);
        return ret;
    }

    private Register emitLoadDouble(Register reg, double c) {
        DataSectionReference ref = new DataSectionReference();
        ref.setOffset(data.position());
        data.emitDouble(c);

        recordDataPatchInCode(ref);
        emitPcaddu12i(scratchRegister, 0xdead >> 12);
        emitAdd(scratchRegister, scratchRegister, 0xdead & 0xfff);
        emitLoadRegister(reg, LoongArch64Kind.DOUBLE, scratchRegister, 0);
        return reg;
    }

    private Register emitLoadFloat(Register reg, float c) {
        DataSectionReference ref = new DataSectionReference();
        ref.setOffset(data.position());
        data.emitFloat(c);

        recordDataPatchInCode(ref);
        emitPcaddu12i(scratchRegister, 0xdead >> 12);
        emitAdd(scratchRegister, scratchRegister, 0xdead & 0xfff);
        emitLoadRegister(reg, LoongArch64Kind.SINGLE, scratchRegister, 0);
        return reg;
    }

    @Override
    public Register emitLoadFloat(float c) {
        Register ret = LoongArch64.fv0;
        return emitLoadFloat(ret, c);
    }

    private Register emitLoadLong(Register reg, long c) {
        emitLi64(reg, c);
        return reg;
    }

    @Override
    public Register emitLoadLong(long c) {
        Register ret = newRegister();
        return emitLoadLong(ret, c);
    }

    private Register emitLoadInt(Register reg, int c) {
        emitLoadImmediate(reg, c);
        return reg;
    }

    @Override
    public Register emitLoadInt(int c) {
        Register ret = newRegister();
        return emitLoadInt(ret, c);
    }

    @Override
    public Register emitIntArg0() {
        return codeCache.getRegisterConfig()
            .getCallingConventionRegisters(HotSpotCallingConventionType.JavaCall, JavaKind.Int)
            .get(0);
    }

    @Override
    public Register emitIntArg1() {
        return codeCache.getRegisterConfig()
            .getCallingConventionRegisters(HotSpotCallingConventionType.JavaCall, JavaKind.Int)
            .get(1);
    }

    @Override
    public Register emitIntAdd(Register a, Register b) {
        emitAdd(a, a, b);
        return a;
    }

    @Override
    public void emitTrap(DebugInfo info) {
        // Dereference null pointer
        emitMove(scratchRegister, LoongArch64.zero);
        recordImplicitException(info);
        emitLoadRegister(LoongArch64.zero, LoongArch64Kind.QWORD, scratchRegister, 0);
    }

    @Override
    public void emitIntRet(Register a) {
        emitMove(LoongArch64.v0, a);
        emitMove(LoongArch64.sp, LoongArch64.fp);
        emitLoadRegister(LoongArch64.ra, LoongArch64Kind.QWORD, LoongArch64.sp, 8);
        emitLoadRegister(LoongArch64.fp, LoongArch64Kind.QWORD, LoongArch64.sp, 0);
        emitGrowStack(-16);
        emitJirl(LoongArch64.zero, LoongArch64.ra, 0);
    }

    @Override
    public void emitFloatRet(Register a) {
        assert a == LoongArch64.fv0 : "Unimplemented move " + a;
        emitMove(LoongArch64.sp, LoongArch64.fp);
        emitLoadRegister(LoongArch64.ra, LoongArch64Kind.QWORD, LoongArch64.sp, 8);
        emitLoadRegister(LoongArch64.fp, LoongArch64Kind.QWORD, LoongArch64.sp, 0);
        emitGrowStack(-16);
        emitJirl(LoongArch64.zero, LoongArch64.ra, 0);
    }

    @Override
    public void emitPointerRet(Register a) {
        emitIntRet(a);
    }

    @Override
    public StackSlot emitPointerToStack(Register a) {
        return emitLongToStack(a);
    }

    @Override
    public StackSlot emitNarrowPointerToStack(Register a) {
        return emitIntToStack(a);
    }

    @Override
    public Register emitUncompressPointer(Register compressed, long base, int shift) {
        if (shift > 0) {
            emitShiftLeft(compressed, compressed, shift);
        }

        if (base != 0) {
            emitLoadLong(scratchRegister, base);
            emitAdd(compressed, compressed, scratchRegister);
        }

        return compressed;
    }

    private StackSlot emitDoubleToStack(StackSlot slot, Register a) {
        emitStoreRegister(a, LoongArch64Kind.DOUBLE, LoongArch64.sp, slot.getOffset(frameSize));
        return slot;
    }

    @Override
    public StackSlot emitDoubleToStack(Register a) {
        StackSlot ret = newStackSlot(LoongArch64Kind.DOUBLE);
        return emitDoubleToStack(ret, a);
    }

    private StackSlot emitFloatToStack(StackSlot slot, Register a) {
        emitStoreRegister(a, LoongArch64Kind.SINGLE, LoongArch64.sp, slot.getOffset(frameSize));
        return slot;
    }

    @Override
    public StackSlot emitFloatToStack(Register a) {
        StackSlot ret = newStackSlot(LoongArch64Kind.SINGLE);
        return emitFloatToStack(ret, a);
    }

    private StackSlot emitIntToStack(StackSlot slot, Register a) {
        emitStoreRegister(a, LoongArch64Kind.DWORD, LoongArch64.sp, slot.getOffset(frameSize));
        return slot;
    }

    @Override
    public StackSlot emitIntToStack(Register a) {
        StackSlot ret = newStackSlot(LoongArch64Kind.DWORD);
        return emitIntToStack(ret, a);
    }

    private StackSlot emitLongToStack(StackSlot slot, Register a) {
        emitStoreRegister(a, LoongArch64Kind.QWORD, LoongArch64.sp, slot.getOffset(frameSize));
        return slot;
    }

    @Override
    public StackSlot emitLongToStack(Register a) {
        StackSlot ret = newStackSlot(LoongArch64Kind.QWORD);
        return emitLongToStack(ret, a);
    }

}
