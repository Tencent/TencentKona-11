/*
 * Copyright (c) 2003, 2014, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2015, 2022, Loongson Technology. All rights reserved.
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
#include "asm/macroAssembler.hpp"
#include "code/vtableStubs.hpp"
#include "interp_masm_mips.hpp"
#include "memory/resourceArea.hpp"
#include "oops/compiledICHolder.hpp"
#include "oops/klass.inline.hpp"
#include "oops/klassVtable.hpp"
#include "runtime/sharedRuntime.hpp"
#include "vmreg_mips.inline.hpp"
#ifdef COMPILER2
#include "opto/runtime.hpp"
#endif


// machine-dependent part of VtableStubs: create VtableStub of correct size and
// initialize its code

#define __ masm->

#define T0 RT0
#define T1 RT1
#define T2 RT2
#define T3 RT3
#define T8 RT8
#define T9 RT9

#ifndef PRODUCT
extern "C" void bad_compiled_vtable_index(JavaThread* thread, oop receiver, int index);
#endif

// used by compiler only;  reciever in T0.
// used registers :
// Rmethod : receiver klass & method
// NOTE: If this code is used by the C1, the receiver_location is always 0.
// when reach here, receiver in T0, klass in T8
VtableStub* VtableStubs::create_vtable_stub(int vtable_index) {
  // Read "A word on VtableStub sizing" in share/code/vtableStubs.hpp for details on stub sizing.
  const int stub_code_length = code_size_limit(true);
  VtableStub* s = new(stub_code_length) VtableStub(true, vtable_index);
  // Can be NULL if there is no free space in the code cache.
  if (s == NULL) {
    return NULL;
  }

  // Count unused bytes in instruction sequences of variable size.
  // We add them to the computed buffer size in order to avoid
  // overflow in subsequently generated stubs.
  address   start_pc;
  int       slop_bytes = 0;
  int       slop_delta = 0;
  int       load_const_maxLen = 6*BytesPerInstWord;  // load_const generates 6 instructions. Assume that as max size for li
  // No variance was detected in vtable stub sizes. Setting index_dependent_slop == 0 will unveil any deviation from this observation.
  const int index_dependent_slop     = 0;

  ResourceMark    rm;
  CodeBuffer      cb(s->entry_point(), stub_code_length);
  MacroAssembler* masm = new MacroAssembler(&cb);
  Register t1 = T8, t2 = Rmethod;
#if (!defined(PRODUCT) && defined(COMPILER2))
  if (CountCompiledCalls) {
    start_pc = __ pc();
    __ li(AT, SharedRuntime::nof_megamorphic_calls_addr());
    slop_delta  = load_const_maxLen - (__ pc() - start_pc);
    slop_bytes += slop_delta;
    assert(slop_delta >= 0, "negative slop(%d) encountered, adjust code size estimate!", slop_delta);
    __ lw(t1, AT , 0);
    __ addiu(t1, t1, 1);
    __ sw(t1, AT,0);
  }
#endif

  // get receiver (need to skip return address on top of stack)
  //assert(receiver_location == T0->as_VMReg(), "receiver expected in T0");

  // get receiver klass
  address npe_addr = __ pc();
  //add for compressedoops
  __ load_klass(t1, T0);

#ifndef PRODUCT
  if (DebugVtables) {
    Label L;
    // check offset vs vtable length
    __ lw(t2, t1, in_bytes(Klass::vtable_length_offset()));
    assert(Assembler::is_simm16(vtable_index*vtableEntry::size()), "change this code");
    __ move(AT, vtable_index*vtableEntry::size());
    __ slt(AT, AT, t2);
    __ bne(AT, R0, L);
    __ delayed()->nop();
    __ move(A2, vtable_index);
    __ move(A1, A0);

    // VTABLE TODO: find upper bound for call_VM length.
    start_pc = __ pc();
    __ call_VM(noreg, CAST_FROM_FN_PTR(address, bad_compiled_vtable_index), A1, A2);
    const ptrdiff_t estimate = 512;
    const ptrdiff_t codesize = __ pc() - start_pc;
    slop_delta  = estimate - codesize;  // call_VM varies in length, depending on data
    assert(slop_delta >= 0, "vtable #%d: Code size estimate (%d) for DebugVtables too small, required: %d", vtable_index, (int)estimate, (int)codesize);
    __ bind(L);
  }
#endif // PRODUCT
  const Register method = Rmethod;

  // load methodOop and target address
  start_pc = __ pc();
  // lookup_virtual_method generates 18 instructions (worst case)
  __ lookup_virtual_method(t1, vtable_index, method);
  slop_delta  = 18*BytesPerInstWord - (int)(__ pc() - start_pc);
  slop_bytes += slop_delta;
  assert(slop_delta >= 0, "negative slop(%d) encountered, adjust code size estimate!", slop_delta);

#ifndef PRODUCT
  if (DebugVtables) {
    Label L;
    __ beq(method, R0, L);
    __ delayed()->nop();
    __ ld(AT, method,in_bytes(Method::from_compiled_offset()));
    __ bne(AT, R0, L);
    __ delayed()->nop();
    __ stop("Vtable entry is NULL");
    __ bind(L);
  }
#endif // PRODUCT

  // T8: receiver klass
  // T0: receiver
  // Rmethod: methodOop
  // T9: entry
  address ame_addr = __ pc();
  __ ld_ptr(T9, method,in_bytes(Method::from_compiled_offset()));
  __ jr(T9);
  __ delayed()->nop();
  masm->flush();
  slop_bytes += index_dependent_slop; // add'l slop for size variance due to large itable offsets
  bookkeeping(masm, tty, s, npe_addr, ame_addr, true, vtable_index, slop_bytes, index_dependent_slop);

  return s;
}


// used registers :
//  T1 T2
// when reach here, the receiver in T0, klass in T1
VtableStub* VtableStubs::create_itable_stub(int itable_index) {
  // Read "A word on VtableStub sizing" in share/code/vtableStubs.hpp for details on stub sizing.
  const int stub_code_length = code_size_limit(false);
  VtableStub* s = new(stub_code_length) VtableStub(false, itable_index);
  // Can be NULL if there is no free space in the code cache.
  if (s == NULL) {
    return NULL;
  }
  // Count unused bytes in instruction sequences of variable size.
  // We add them to the computed buffer size in order to avoid
  // overflow in subsequently generated stubs.
  address   start_pc;
  int       slop_bytes = 0;
  int       slop_delta = 0;
  int       load_const_maxLen = 6*BytesPerInstWord;  // load_const generates 6 instructions. Assume that as max size for li

  ResourceMark    rm;
  CodeBuffer      cb(s->entry_point(), stub_code_length);
  MacroAssembler *masm = new MacroAssembler(&cb);

  // we T8,T9 as temparary register, they are free from register allocator
  Register t1 = T8, t2 = T2;
  // Entry arguments:
  //  T1: Interface
  //  T0: Receiver

#if (!defined(PRODUCT) && defined(COMPILER2))
  if (CountCompiledCalls) {
    start_pc = __ pc();
    __ li(AT, SharedRuntime::nof_megamorphic_calls_addr());
    slop_delta  = load_const_maxLen - (__ pc() - start_pc);
    slop_bytes += slop_delta;
    assert(slop_delta >= 0, "negative slop(%d) encountered, adjust code size estimate!", slop_delta);
    __ lw(T8, AT, 0);
    __ addiu(T8, T8,1);
    __ sw(T8, AT, 0);
  }
#endif // PRODUCT

  const Register holder_klass_reg   = T1; // declaring interface klass (DECC)
  const Register resolved_klass_reg = Rmethod; // resolved interface klass (REFC)

  const Register icholder_reg = T1;
  __ ld_ptr(resolved_klass_reg, icholder_reg, CompiledICHolder::holder_klass_offset());
  __ ld_ptr(holder_klass_reg,   icholder_reg, CompiledICHolder::holder_metadata_offset());

  Label L_no_such_interface;

  // get receiver klass (also an implicit null-check)
  address npe_addr = __ pc();
  __ load_klass(t1, T0);
  {
    // x86 use lookup_interface_method, but lookup_interface_method does not work on MIPS.
    // No dynamic code size variance here, so slop_bytes is not needed.
    const int base = in_bytes(Klass::vtable_start_offset());
    assert(vtableEntry::size() * wordSize == 8, "adjust the scaling in the code below");
    assert(Assembler::is_simm16(base), "change this code");
    __ daddiu(t2, t1, base);
    __ lw(AT, t1, in_bytes(Klass::vtable_length_offset()));
    __ dsll(AT, AT, Address::times_8);
    __ daddu(t2, t2, AT);
    if (HeapWordsPerLong > 1) {
      __ round_to(t2, BytesPerLong);
    }

    Label hit, entry;
    __ bind(entry);

    // Check that the entry is non-null.  A null entry means that
    // the receiver class doesn't implement the interface, and wasn't the
    // same as when the caller was compiled.
    __ ld_ptr(AT, t2, itableOffsetEntry::interface_offset_in_bytes());
    __ beq(AT, R0, L_no_such_interface);
    __ delayed()->nop();

    __ bne(AT, resolved_klass_reg, entry);
    __ delayed()->addiu(t2, t2, itableOffsetEntry::size() * wordSize);

  }

  // add for compressedoops
  __ load_klass(t1, T0);
  // compute itable entry offset (in words)
  const int base = in_bytes(Klass::vtable_start_offset());
  __ daddiu(t2, t1, base);
  __ lw(AT, t1, in_bytes(Klass::vtable_length_offset()));
  __ dsll(AT, AT, Address::times_8);
  __ daddu(t2, t2, AT);
  if (HeapWordsPerLong > 1) {
    __ round_to(t2, BytesPerLong);
  }

  Label hit, entry;
  __ bind(entry);

  // Check that the entry is non-null.  A null entry means that
  // the receiver class doesn't implement the interface, and wasn't the
  // same as when the caller was compiled.
  __ ld_ptr(AT, t2, itableOffsetEntry::interface_offset_in_bytes());
  __ beq(AT, R0, L_no_such_interface);
  __ delayed()->nop();

  __ bne(AT, holder_klass_reg, entry);
  __ delayed()->addiu(t2, t2, itableOffsetEntry::size() * wordSize);

  // We found a hit, move offset into T9
  __ ld_ptr(t2, t2, itableOffsetEntry::offset_offset_in_bytes() - itableOffsetEntry::size() * wordSize);

  // Compute itableMethodEntry.
  const int method_offset = (itableMethodEntry::size() * wordSize * itable_index) +
    itableMethodEntry::method_offset_in_bytes();

  // Get methodOop and entrypoint for compiler
  const Register method = Rmethod;
  __ dsll(AT, t2, Address::times_1);
  __ addu(AT, AT, t1 );
  start_pc = __ pc();
  __ set64(t1, method_offset);
  slop_delta  = load_const_maxLen - (__ pc() - start_pc);
  slop_bytes += slop_delta;
  assert(slop_delta >= 0, "negative slop(%d) encountered, adjust code size estimate!", slop_delta);
  __ addu(AT, AT, t1 );
  __ ld_ptr(method, AT, 0);

#ifdef ASSERT
  if (DebugVtables) {
    Label L1;
    __ beq(method, R0, L1);
    __ delayed()->nop();
    __ ld(AT, method,in_bytes(Method::from_compiled_offset()));
    __ bne(AT, R0, L1);
    __ delayed()->nop();
    __ stop("methodOop is null");
    __ bind(L1);
  }
#endif // ASSERT

  // Rmethod: methodOop
  // T0: receiver
  // T9: entry point
  address ame_addr = __ pc();
  __ ld_ptr(T9, method,in_bytes(Method::from_compiled_offset()));
  __ jr(T9);
  __ delayed()->nop();

  __ bind(L_no_such_interface);
  // Handle IncompatibleClassChangeError in itable stubs.
  // More detailed error message.
  // We force resolving of the call site by jumping to the "handle
  // wrong method" stub, and so let the interpreter runtime do all the
  // dirty work.
  start_pc = __ pc();
  __ set64(T9, (long)SharedRuntime::get_handle_wrong_method_stub());
  slop_delta  = load_const_maxLen - (__ pc() - start_pc);
  slop_bytes += slop_delta;
  assert(slop_delta >= 0, "negative slop(%d) encountered, adjust code size estimate!", slop_delta);
  __ jr(T9);
  __ delayed()->nop();

  masm->flush();
  bookkeeping(masm, tty, s, npe_addr, ame_addr, false, itable_index, slop_bytes, 0);

  return s;
}

// NOTE : whenever you change the code above, dont forget to change the const here
int VtableStub::pd_code_alignment() {
  const unsigned int icache_line_size = wordSize;
  return icache_line_size;
}
