/*
 * Copyright (c) 1997, 2018, Oracle and/or its affiliates. All rights reserved.
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
#include "asm/macroAssembler.inline.hpp"
#include "code/compiledIC.hpp"
#include "code/icBuffer.hpp"
#include "code/nmethod.hpp"
#include "memory/resourceArea.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/safepoint.hpp"
#ifdef COMPILER2
#include "opto/matcher.hpp"
#endif

// ----------------------------------------------------------------------------

#define __ _masm.
address CompiledStaticCall::emit_to_interp_stub(CodeBuffer &cbuf, address mark) {
  // Stub is fixed up when the corresponding call is converted from calling
  // compiled code to calling interpreted code.
  // set (empty), G5
  // jmp -1

  if (mark == NULL) {
    mark = cbuf.insts_mark();  // Get mark within main instrs section.
  }

  MacroAssembler _masm(&cbuf);

  address base = __ start_a_stub(to_interp_stub_size());
  if (base == NULL) {
    return NULL;  // CodeBuffer::expand failed.
  }

  // Static stub relocation stores the instruction address of the call.
  __ relocate(static_stub_Relocation::spec(mark));

  __ set_metadata(NULL, as_Register(Matcher::inline_cache_reg_encode()));

  __ set_inst_mark();
  AddressLiteral addrlit(-1);
  __ JUMP(addrlit, G3, 0);

  __ delayed()->nop();

  assert(__ pc() - base <= to_interp_stub_size(), "wrong stub size");

  // Update current stubs pointer and restore code_end.
  __ end_a_stub();
  return base;
}
#undef __

int CompiledStaticCall::to_trampoline_stub_size() {
  // SPARC doesn't use trampolines.
  return 0;
}

int CompiledStaticCall::to_interp_stub_size() {
  // This doesn't need to be accurate but it must be larger or equal to
  // the real size of the stub.
  return (NativeMovConstReg::instruction_size +  // sethi/setlo;
          NativeJump::instruction_size); // sethi; jmp; nop
}

// Relocation entries for call stub, compiled java to interpreter.
int CompiledStaticCall::reloc_to_interp_stub() {
  return 10;  // 4 in emit_java_to_interp + 1 in Java_Static_Call
}

void CompiledDirectStaticCall::set_to_interpreted(const methodHandle& callee, address entry) {
  address stub = find_stub(/*is_aot*/ false);
  guarantee(stub != NULL, "stub not found");

  if (TraceICs) {
    ResourceMark rm;
    tty->print_cr("CompiledDirectStaticCall@" INTPTR_FORMAT ": set_to_interpreted %s",
                  p2i(instruction_address()),
                  callee->name_and_sig_as_C_string());
  }

  // Creation also verifies the object.
  NativeMovConstReg* method_holder = nativeMovConstReg_at(stub);
  NativeJump*        jump          = nativeJump_at(method_holder->next_instruction_address());

#ifdef ASSERT
  // read the value once
  volatile intptr_t data = method_holder->data();
  volatile address destination = jump->jump_destination();
  assert(data == 0 || data == (intptr_t)callee(),
         "a) MT-unsafe modification of inline cache");
  assert(destination == (address)-1 || destination == entry,
         "b) MT-unsafe modification of inline cache");
#endif

  // Update stub.
  method_holder->set_data((intptr_t)callee());
  jump->set_jump_destination(entry);

  // Update jump to call.
  set_destination_mt_safe(stub);
}

void CompiledDirectStaticCall::set_stub_to_clean(static_stub_Relocation* static_stub) {
  // Reset stub.
  address stub = static_stub->addr();
  assert(stub != NULL, "stub not found");
  assert(CompiledICLocker::is_safe(stub), "mt unsafe call");
  // Creation also verifies the object.
  NativeMovConstReg* method_holder = nativeMovConstReg_at(stub);
  NativeJump*        jump          = nativeJump_at(method_holder->next_instruction_address());
  method_holder->set_data(0);
  jump->set_jump_destination((address)-1);
}

//-----------------------------------------------------------------------------
// Non-product mode code
#ifndef PRODUCT

void CompiledDirectStaticCall::verify() {
  // Verify call.
  _call->verify();
  if (os::is_MP()) {
    _call->verify_alignment();
  }

  // Verify stub.
  address stub = find_stub(/*is_aot*/ false);
  assert(stub != NULL, "no stub found for static call");
  // Creation also verifies the object.
  NativeMovConstReg* method_holder = nativeMovConstReg_at(stub);
  NativeJump*        jump          = nativeJump_at(method_holder->next_instruction_address());

  // Verify state.
  assert(is_clean() || is_call_to_compiled() || is_call_to_interpreted(), "sanity check");
}

#endif // !PRODUCT
