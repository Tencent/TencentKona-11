/*
 * Copyright (c) 2010, 2016, Oracle and/or its affiliates. All rights reserved.
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
 *
 */

#include "precompiled.hpp"
#include "c1/c1_LIR.hpp"

FloatRegister LIR_OprDesc::as_float_reg() const {
  return as_FloatRegister(fpu_regnr());
}

FloatRegister LIR_OprDesc::as_double_reg() const {
  return as_FloatRegister(fpu_regnrLo());
}

#ifdef AARCH64
// Reg2 unused.
LIR_Opr LIR_OprFact::double_fpu(int reg1, int reg2) {
  assert(as_FloatRegister(reg2) == fnoreg, "Not used on this platform");
  return (LIR_Opr)(intptr_t)((reg1 << LIR_OprDesc::reg1_shift) |
                             (reg1 << LIR_OprDesc::reg2_shift) |
                             LIR_OprDesc::double_type          |
                             LIR_OprDesc::fpu_register         |
                             LIR_OprDesc::double_size);
}
#else
LIR_Opr LIR_OprFact::double_fpu(int reg1, int reg2) {
  assert(as_FloatRegister(reg2) != fnoreg, "Arm32 holds double in two regs.");
  return (LIR_Opr)(intptr_t)((reg1 << LIR_OprDesc::reg1_shift) |
                             (reg2 << LIR_OprDesc::reg2_shift) |
                             LIR_OprDesc::double_type          |
                             LIR_OprDesc::fpu_register         |
                             LIR_OprDesc::double_size);
}
#endif

#ifndef PRODUCT
void LIR_Address::verify() const {
#ifdef _LP64
  assert(base()->is_cpu_register(), "wrong base operand");
#endif
#ifdef AARCH64
  if (base()->type() == T_INT) {
    assert(index()->is_single_cpu() && (index()->type() == T_INT), "wrong index operand");
  } else {
    assert(index()->is_illegal() || index()->is_double_cpu() ||
           (index()->is_single_cpu() && (index()->is_oop_register() || index()->type() == T_INT)), "wrong index operand");
    assert(base()->type() == T_OBJECT || base()->type() == T_LONG || base()->type() == T_METADATA, "wrong type for addresses");
  }
#else
  assert(disp() == 0 || index()->is_illegal(), "can't have both");
  // Note: offsets higher than 4096 must not be rejected here. They can
  // be handled by the back-end or will be rejected if not.
#ifdef _LP64
  assert(index()->is_illegal() || index()->is_double_cpu(), "wrong index operand");
  assert(base()->type() == T_OBJECT || base()->type() == T_LONG || base()->type() == T_METADATA,
         "wrong type for addresses");
#else
  assert(base()->is_single_cpu(), "wrong base operand");
  assert(index()->is_illegal() || index()->is_single_cpu(), "wrong index operand");
  assert(base()->type() == T_OBJECT || base()->type() == T_INT || base()->type() == T_METADATA,
         "wrong type for addresses");
#endif
#endif // AARCH64
}
#endif // PRODUCT

template<typename T>
void LIR_List::cmp_branch(LIR_Condition condition, LIR_Opr left, LIR_Opr right, BasicType type, T tgt, CodeEmitInfo* info) {
  cmp(condition, left, right, info);
  branch(condition, type, tgt);
}

// Explicit instantiation for all supported types.
template void LIR_List::cmp_branch(LIR_Condition, LIR_Opr, LIR_Opr, BasicType type, Label*, CodeEmitInfo*);
template void LIR_List::cmp_branch(LIR_Condition, LIR_Opr, LIR_Opr, BasicType type, BlockBegin*, CodeEmitInfo*);
template void LIR_List::cmp_branch(LIR_Condition, LIR_Opr, LIR_Opr, BasicType type, CodeStub*, CodeEmitInfo*);

void LIR_List::cmp_branch(LIR_Condition condition, LIR_Opr left, LIR_Opr right, BasicType type, BlockBegin* block, BlockBegin* unordered) {
  cmp(condition, left, right);
  branch(condition, type, block, unordered);
}

void LIR_List::cmp_cmove(LIR_Condition condition, LIR_Opr left, LIR_Opr right, LIR_Opr src1, LIR_Opr src2, LIR_Opr dst, BasicType type) {
  cmp(condition, left, right);
  cmove(condition, src1, src2, dst, type);
}
