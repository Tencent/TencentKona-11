/*
 * Copyright (c) 1997, 2014, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2017, 2022, Loongson Technology. All rights reserved.
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
#include "jvm.h"
#include "asm/assembler.hpp"
#include "asm/assembler.inline.hpp"
#include "asm/macroAssembler.inline.hpp"
#include "compiler/disassembler.hpp"
#include "gc/shared/barrierSet.hpp"
#include "gc/shared/barrierSetAssembler.hpp"
#include "gc/shared/collectedHeap.inline.hpp"
#include "interpreter/interpreter.hpp"
#include "memory/resourceArea.hpp"
#include "memory/universe.hpp"
#include "nativeInst_loongarch.hpp"
#include "prims/methodHandles.hpp"
#include "runtime/biasedLocking.hpp"
#include "runtime/interfaceSupport.inline.hpp"
#include "runtime/objectMonitor.hpp"
#include "runtime/os.hpp"
#include "runtime/safepoint.hpp"
#include "runtime/safepointMechanism.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/stubRoutines.hpp"
#include "utilities/macros.hpp"

#ifdef COMPILER2
#include "opto/compile.hpp"
#include "opto/intrinsicnode.hpp"
#endif

#define T0 RT0
#define T1 RT1
#define T2 RT2
#define T3 RT3
#define T4 RT4
#define T5 RT5
#define T6 RT6
#define T7 RT7
#define T8 RT8

// Implementation of MacroAssembler

intptr_t MacroAssembler::i[32] = {0};
float MacroAssembler::f[32] = {0.0};

void MacroAssembler::print(outputStream *s) {
  unsigned int k;
  for(k=0; k<sizeof(i)/sizeof(i[0]); k++) {
    s->print_cr("i%d = 0x%.16lx", k, i[k]);
  }
  s->cr();

  for(k=0; k<sizeof(f)/sizeof(f[0]); k++) {
    s->print_cr("f%d = %f", k, f[k]);
  }
  s->cr();
}

int MacroAssembler::i_offset(unsigned int k) { return (intptr_t)&((MacroAssembler*)0)->i[k]; }
int MacroAssembler::f_offset(unsigned int k) { return (intptr_t)&((MacroAssembler*)0)->f[k]; }

void MacroAssembler::save_registers(MacroAssembler *masm) {
#define __ masm->
  for(int k=0; k<32; k++) {
    __ st_w (as_Register(k), A0, i_offset(k));
  }

  for(int k=0; k<32; k++) {
    __ fst_s (as_FloatRegister(k), A0, f_offset(k));
  }
#undef __
}

void MacroAssembler::restore_registers(MacroAssembler *masm) {
#define __ masm->
  for(int k=0; k<32; k++) {
    __ ld_w (as_Register(k), A0, i_offset(k));
  }

  for(int k=0; k<32; k++) {
    __ fld_s (as_FloatRegister(k), A0, f_offset(k));
  }
#undef __
}


void MacroAssembler::pd_patch_instruction(address branch, address target) {
  jint& stub_inst = *(jint*)branch;
  jint *pc = (jint *)branch;

  if (high(stub_inst, 7) == pcaddu18i_op) {
    // far:
    //   pcaddu18i reg, si20
    //   jirl  r0, reg, si18

    assert(high(pc[1], 6) == jirl_op, "Not a branch label patch");
    jlong offs = target - branch;
    CodeBuffer cb(branch, 2 * BytesPerInstWord);
    MacroAssembler masm(&cb);
    if (reachable_from_branch_short(offs)) {
      // convert far to short
#define __ masm.
      __ b(target);
      __ nop();
#undef __
    } else {
      masm.patchable_jump_far(R0, offs);
    }
    return;
  } else if (high(stub_inst, 7) == pcaddi_op) {
    // see MacroAssembler::set_last_Java_frame:
    //   pcaddi reg, si20

    jint offs = (target - branch) >> 2;
    guarantee(is_simm(offs, 20), "Not signed 20-bit offset");
    CodeBuffer cb(branch, 1 * BytesPerInstWord);
    MacroAssembler masm(&cb);
    masm.pcaddi(as_Register(low(stub_inst, 5)), offs);
    return;
  } else if (high(stub_inst, 7) == pcaddu12i_op) {
    // pc-relative
    jlong offs = target - branch;
    guarantee(is_simm(offs, 32), "Not signed 32-bit offset");
    jint si12, si20;
    jint& stub_instNext = *(jint*)(branch+4);
    split_simm32(offs, si12, si20);
    CodeBuffer cb(branch, 2 * BytesPerInstWord);
    MacroAssembler masm(&cb);
    masm.pcaddu12i(as_Register(low(stub_inst, 5)), si20);
    masm.addi_d(as_Register(low((stub_instNext), 5)), as_Register(low((stub_instNext) >> 5, 5)), si12);
    return;
  } else if (high(stub_inst, 7) == lu12i_w_op) {
    // long call (absolute)
    CodeBuffer cb(branch, 3 * BytesPerInstWord);
    MacroAssembler masm(&cb);
    masm.call_long(target);
    return;
  }

  stub_inst = patched_branch(target - branch, stub_inst, 0);
}

bool MacroAssembler::reachable_from_branch_short(jlong offs) {
  if (ForceUnreachable) {
    return false;
  }
  return is_simm(offs >> 2, 26);
}

void MacroAssembler::patchable_jump_far(Register ra, jlong offs) {
  jint si18, si20;
  guarantee(is_simm(offs, 38), "Not signed 38-bit offset");
  split_simm38(offs, si18, si20);
  pcaddu18i(T4, si20);
  jirl(ra, T4, si18);
}

void MacroAssembler::patchable_jump(address target, bool force_patchable) {
  assert(ReservedCodeCacheSize < 4*G, "branch out of range");
  assert(CodeCache::find_blob(target) != NULL,
         "destination of jump not found in code cache");
  if (force_patchable || patchable_branches()) {
    jlong offs = target - pc();
    if (reachable_from_branch_short(offs)) { // Short jump
      b(offset26(target));
      nop();
    } else {                                 // Far jump
      patchable_jump_far(R0, offs);
    }
  } else {                                   // Real short jump
    b(offset26(target));
  }
}

void MacroAssembler::patchable_call(address target, address call_site) {
  jlong offs = target - (call_site ? call_site : pc());
  if (reachable_from_branch_short(offs - BytesPerInstWord)) { // Short call
    nop();
    bl((offs - BytesPerInstWord) >> 2);
  } else {                                                    // Far call
    patchable_jump_far(RA, offs);
  }
}

// Maybe emit a call via a trampoline.  If the code cache is small
// trampolines won't be emitted.

address MacroAssembler::trampoline_call(AddressLiteral entry, CodeBuffer *cbuf) {
  assert(JavaThread::current()->is_Compiler_thread(), "just checking");
  assert(entry.rspec().type() == relocInfo::runtime_call_type
         || entry.rspec().type() == relocInfo::opt_virtual_call_type
         || entry.rspec().type() == relocInfo::static_call_type
         || entry.rspec().type() == relocInfo::virtual_call_type, "wrong reloc type");

  // We need a trampoline if branches are far.
  if (far_branches()) {
    bool in_scratch_emit_size = false;
#ifdef COMPILER2
    // We don't want to emit a trampoline if C2 is generating dummy
    // code during its branch shortening phase.
    CompileTask* task = ciEnv::current()->task();
    in_scratch_emit_size =
      (task != NULL && is_c2_compile(task->comp_level()) &&
       Compile::current()->in_scratch_emit_size());
#endif
    if (!in_scratch_emit_size) {
      address stub = emit_trampoline_stub(offset(), entry.target());
      if (stub == NULL) {
        postcond(pc() == badAddress);
        return NULL; // CodeCache is full
      }
    }
  }

  if (cbuf) cbuf->set_insts_mark();
  relocate(entry.rspec());
  if (!far_branches()) {
    bl(entry.target());
  } else {
    bl(pc());
  }
  // just need to return a non-null address
  postcond(pc() != badAddress);
  return pc();
}

// Emit a trampoline stub for a call to a target which is too far away.
//
// code sequences:
//
// call-site:
//   branch-and-link to <destination> or <trampoline stub>
//
// Related trampoline stub for this call site in the stub section:
//   load the call target from the constant pool
//   branch (RA still points to the call site above)

address MacroAssembler::emit_trampoline_stub(int insts_call_instruction_offset,
                                             address dest) {
  // Start the stub
  address stub = start_a_stub(NativeInstruction::nop_instruction_size
                   + NativeCallTrampolineStub::instruction_size);
  if (stub == NULL) {
    return NULL;  // CodeBuffer::expand failed
  }

  // Create a trampoline stub relocation which relates this trampoline stub
  // with the call instruction at insts_call_instruction_offset in the
  // instructions code-section.
  align(wordSize);
  relocate(trampoline_stub_Relocation::spec(code()->insts()->start()
                                            + insts_call_instruction_offset));
  const int stub_start_offset = offset();

  // Now, create the trampoline stub's code:
  // - load the call
  // - call
  pcaddi(T4, 0);
  ld_d(T4, T4, 16);
  jr(T4);
  nop();  //align
  assert(offset() - stub_start_offset == NativeCallTrampolineStub::data_offset,
         "should be");
  emit_int64((int64_t)dest);

  const address stub_start_addr = addr_at(stub_start_offset);

  NativeInstruction* ni = nativeInstruction_at(stub_start_addr);
  assert(ni->is_NativeCallTrampolineStub_at(), "doesn't look like a trampoline");

  end_a_stub();
  return stub_start_addr;
}

void MacroAssembler::beq_far(Register rs, Register rt, address entry) {
  if (is_simm16((entry - pc()) >> 2)) { // Short jump
    beq(rs, rt, offset16(entry));
  } else {                              // Far jump
    Label not_jump;
    bne(rs, rt, not_jump);
    b_far(entry);
    bind(not_jump);
  }
}

void MacroAssembler::beq_far(Register rs, Register rt, Label& L) {
  if (L.is_bound()) {
    beq_far(rs, rt, target(L));
  } else {
    Label not_jump;
    bne(rs, rt, not_jump);
    b_far(L);
    bind(not_jump);
  }
}

void MacroAssembler::bne_far(Register rs, Register rt, address entry) {
  if (is_simm16((entry - pc()) >> 2)) { // Short jump
    bne(rs, rt, offset16(entry));
  } else {                              // Far jump
    Label not_jump;
    beq(rs, rt, not_jump);
    b_far(entry);
    bind(not_jump);
  }
}

void MacroAssembler::bne_far(Register rs, Register rt, Label& L) {
  if (L.is_bound()) {
    bne_far(rs, rt, target(L));
  } else {
    Label not_jump;
    beq(rs, rt, not_jump);
    b_far(L);
    bind(not_jump);
  }
}

void MacroAssembler::blt_far(Register rs, Register rt, address entry, bool is_signed) {
  if (is_simm16((entry - pc()) >> 2)) { // Short jump
    if (is_signed) {
      blt(rs, rt, offset16(entry));
    } else {
      bltu(rs, rt, offset16(entry));
    }
  } else {                              // Far jump
    Label not_jump;
    if (is_signed) {
      bge(rs, rt, not_jump);
    } else {
      bgeu(rs, rt, not_jump);
    }
    b_far(entry);
    bind(not_jump);
  }
}

void MacroAssembler::blt_far(Register rs, Register rt, Label& L, bool is_signed) {
  if (L.is_bound()) {
    blt_far(rs, rt, target(L), is_signed);
  } else {
    Label not_jump;
    if (is_signed) {
      bge(rs, rt, not_jump);
    } else {
      bgeu(rs, rt, not_jump);
    }
    b_far(L);
    bind(not_jump);
  }
}

void MacroAssembler::bge_far(Register rs, Register rt, address entry, bool is_signed) {
  if (is_simm16((entry - pc()) >> 2)) { // Short jump
    if (is_signed) {
      bge(rs, rt, offset16(entry));
    } else {
      bgeu(rs, rt, offset16(entry));
    }
  } else {                              // Far jump
    Label not_jump;
    if (is_signed) {
      blt(rs, rt, not_jump);
    } else {
      bltu(rs, rt, not_jump);
    }
    b_far(entry);
    bind(not_jump);
  }
}

void MacroAssembler::bge_far(Register rs, Register rt, Label& L, bool is_signed) {
  if (L.is_bound()) {
    bge_far(rs, rt, target(L), is_signed);
  } else {
    Label not_jump;
    if (is_signed) {
      blt(rs, rt, not_jump);
    } else {
      bltu(rs, rt, not_jump);
    }
    b_far(L);
    bind(not_jump);
  }
}

void MacroAssembler::beq_long(Register rs, Register rt, Label& L) {
  Label not_taken;
  bne(rs, rt, not_taken);
  jmp_far(L);
  bind(not_taken);
}

void MacroAssembler::bne_long(Register rs, Register rt, Label& L) {
  Label not_taken;
  beq(rs, rt, not_taken);
  jmp_far(L);
  bind(not_taken);
}

void MacroAssembler::bc1t_long(Label& L) {
  Label not_taken;
  bceqz(FCC0, not_taken);
  jmp_far(L);
  bind(not_taken);
}

void MacroAssembler::blt_long(Register rs, Register rt, Label& L, bool is_signed) {
  Label not_taken;
  if (is_signed) {
    bge(rs, rt, not_taken);
  } else {
    bgeu(rs, rt, not_taken);
  }
  jmp_far(L);
  bind(not_taken);
}

void MacroAssembler::bge_long(Register rs, Register rt, Label& L, bool is_signed) {
  Label not_taken;
  if (is_signed) {
    blt(rs, rt, not_taken);
  } else {
    bltu(rs, rt, not_taken);
  }
  jmp_far(L);
  bind(not_taken);
}

void MacroAssembler::bc1f_long(Label& L) {
  Label not_taken;
  bcnez(FCC0, not_taken);
  jmp_far(L);
  bind(not_taken);
}

void MacroAssembler::b_far(Label& L) {
  if (L.is_bound()) {
    b_far(target(L));
  } else {
    L.add_patch_at(code(), locator());
    if (ForceUnreachable) {
      patchable_jump_far(R0, 0);
    } else {
      b(0);
    }
  }
}

void MacroAssembler::b_far(address entry) {
  jlong offs = entry - pc();
  if (reachable_from_branch_short(offs)) { // Short jump
    b(offset26(entry));
  } else {                                 // Far jump
    patchable_jump_far(R0, offs);
  }
}

void MacroAssembler::ld_ptr(Register rt, Register base, Register offset) {
  ldx_d(rt, base, offset);
}

void MacroAssembler::st_ptr(Register rt, Register base, Register offset) {
  stx_d(rt, base, offset);
}

Address MacroAssembler::as_Address(AddressLiteral adr) {
  return Address(adr.target(), adr.rspec());
}

Address MacroAssembler::as_Address(ArrayAddress adr) {
  return Address::make_array(adr);
}

// tmp_reg1 and tmp_reg2 should be saved outside of atomic_inc32 (caller saved).
void MacroAssembler::atomic_inc32(address counter_addr, int inc, Register tmp_reg1, Register tmp_reg2) {
  li(tmp_reg1, inc);
  li(tmp_reg2, counter_addr);
  amadd_w(R0, tmp_reg1, tmp_reg2);
}

void MacroAssembler::reserved_stack_check() {
  Register thread = TREG;
#ifndef OPT_THREAD
  get_thread(thread);
#endif
  // testing if reserved zone needs to be enabled
  Label no_reserved_zone_enabling;

  ld_d(AT, Address(thread, JavaThread::reserved_stack_activation_offset()));
  sub_d(AT, SP, AT);
  blt(AT, R0,  no_reserved_zone_enabling);

  enter();   // RA and FP are live.
  call_VM_leaf(CAST_FROM_FN_PTR(address, SharedRuntime::enable_stack_reserved_zone), thread);
  leave();

  // We have already removed our own frame.
  // throw_delayed_StackOverflowError will think that it's been
  // called by our caller.
  li(AT, (long)StubRoutines::throw_delayed_StackOverflowError_entry());
  jr(AT);
  should_not_reach_here();

  bind(no_reserved_zone_enabling);
}

int MacroAssembler::biased_locking_enter(Register lock_reg,
                                         Register obj_reg,
                                         Register swap_reg,
                                         Register tmp_reg,
                                         bool swap_reg_contains_mark,
                                         Label& done,
                                         Label* slow_case,
                                         BiasedLockingCounters* counters) {
  assert(UseBiasedLocking, "why call this otherwise?");
  bool need_tmp_reg = false;
  if (tmp_reg == noreg) {
    need_tmp_reg = true;
    tmp_reg = T4;
  }
  assert_different_registers(lock_reg, obj_reg, swap_reg, tmp_reg, AT);
  assert(markOopDesc::age_shift == markOopDesc::lock_bits + markOopDesc::biased_lock_bits, "biased locking makes assumptions about bit layout");
  Address mark_addr      (obj_reg, oopDesc::mark_offset_in_bytes());
  Address saved_mark_addr(lock_reg, 0);

  // Biased locking
  // See whether the lock is currently biased toward our thread and
  // whether the epoch is still valid
  // Note that the runtime guarantees sufficient alignment of JavaThread
  // pointers to allow age to be placed into low bits
  // First check to see whether biasing is even enabled for this object
  Label cas_label;
  int null_check_offset = -1;
  if (!swap_reg_contains_mark) {
    null_check_offset = offset();
    ld_ptr(swap_reg, mark_addr);
  }

  if (need_tmp_reg) {
    push(tmp_reg);
  }
  move(tmp_reg, swap_reg);
  andi(tmp_reg, tmp_reg, markOopDesc::biased_lock_mask_in_place);
  addi_d(AT, R0, markOopDesc::biased_lock_pattern);
  sub_d(AT, AT, tmp_reg);
  if (need_tmp_reg) {
    pop(tmp_reg);
  }

  bne(AT, R0, cas_label);


  // The bias pattern is present in the object's header. Need to check
  // whether the bias owner and the epoch are both still current.
  // Note that because there is no current thread register on LA we
  // need to store off the mark word we read out of the object to
  // avoid reloading it and needing to recheck invariants below. This
  // store is unfortunate but it makes the overall code shorter and
  // simpler.
  st_ptr(swap_reg, saved_mark_addr);
  if (need_tmp_reg) {
    push(tmp_reg);
  }
  if (swap_reg_contains_mark) {
    null_check_offset = offset();
  }
  load_prototype_header(tmp_reg, obj_reg);
  xorr(tmp_reg, tmp_reg, swap_reg);
#ifndef OPT_THREAD
  get_thread(swap_reg);
  xorr(swap_reg, swap_reg, tmp_reg);
#else
  xorr(swap_reg, TREG, tmp_reg);
#endif

  li(AT, ~((int) markOopDesc::age_mask_in_place));
  andr(swap_reg, swap_reg, AT);

  if (PrintBiasedLockingStatistics) {
    Label L;
    bne(swap_reg, R0, L);
    push(tmp_reg);
    push(A0);
    atomic_inc32((address)BiasedLocking::biased_lock_entry_count_addr(), 1, A0, tmp_reg);
    pop(A0);
    pop(tmp_reg);
    bind(L);
  }
  if (need_tmp_reg) {
    pop(tmp_reg);
  }
  beq(swap_reg, R0, done);
  Label try_revoke_bias;
  Label try_rebias;

  // At this point we know that the header has the bias pattern and
  // that we are not the bias owner in the current epoch. We need to
  // figure out more details about the state of the header in order to
  // know what operations can be legally performed on the object's
  // header.

  // If the low three bits in the xor result aren't clear, that means
  // the prototype header is no longer biased and we have to revoke
  // the bias on this object.

  li(AT, markOopDesc::biased_lock_mask_in_place);
  andr(AT, swap_reg, AT);
  bne(AT, R0, try_revoke_bias);
  // Biasing is still enabled for this data type. See whether the
  // epoch of the current bias is still valid, meaning that the epoch
  // bits of the mark word are equal to the epoch bits of the
  // prototype header. (Note that the prototype header's epoch bits
  // only change at a safepoint.) If not, attempt to rebias the object
  // toward the current thread. Note that we must be absolutely sure
  // that the current epoch is invalid in order to do this because
  // otherwise the manipulations it performs on the mark word are
  // illegal.

  li(AT, markOopDesc::epoch_mask_in_place);
  andr(AT,swap_reg, AT);
  bne(AT, R0, try_rebias);
  // The epoch of the current bias is still valid but we know nothing
  // about the owner; it might be set or it might be clear. Try to
  // acquire the bias of the object using an atomic operation. If this
  // fails we will go in to the runtime to revoke the object's bias.
  // Note that we first construct the presumed unbiased header so we
  // don't accidentally blow away another thread's valid bias.

  ld_ptr(swap_reg, saved_mark_addr);

  li(AT, markOopDesc::biased_lock_mask_in_place | markOopDesc::age_mask_in_place | markOopDesc::epoch_mask_in_place);
  andr(swap_reg, swap_reg, AT);

  if (need_tmp_reg) {
    push(tmp_reg);
  }
#ifndef OPT_THREAD
  get_thread(tmp_reg);
  orr(tmp_reg, tmp_reg, swap_reg);
#else
  orr(tmp_reg, TREG, swap_reg);
#endif
  cmpxchg(Address(obj_reg, 0), swap_reg, tmp_reg, AT, false, false);
  if (need_tmp_reg) {
    pop(tmp_reg);
  }
  // If the biasing toward our thread failed, this means that
  // another thread succeeded in biasing it toward itself and we
  // need to revoke that bias. The revocation will occur in the
  // interpreter runtime in the slow case.
  if (PrintBiasedLockingStatistics) {
    Label L;
    bne(AT, R0, L);
    push(tmp_reg);
    push(A0);
    atomic_inc32((address)BiasedLocking::anonymously_biased_lock_entry_count_addr(), 1, A0, tmp_reg);
    pop(A0);
    pop(tmp_reg);
    bind(L);
  }
  if (slow_case != NULL) {
    beq_far(AT, R0, *slow_case);
  }
  b(done);

  bind(try_rebias);
  // At this point we know the epoch has expired, meaning that the
  // current "bias owner", if any, is actually invalid. Under these
  // circumstances _only_, we are allowed to use the current header's
  // value as the comparison value when doing the cas to acquire the
  // bias in the current epoch. In other words, we allow transfer of
  // the bias from one thread to another directly in this situation.
  //
  // FIXME: due to a lack of registers we currently blow away the age
  // bits in this situation. Should attempt to preserve them.
  if (need_tmp_reg) {
    push(tmp_reg);
  }
  load_prototype_header(tmp_reg, obj_reg);
#ifndef OPT_THREAD
  get_thread(swap_reg);
  orr(tmp_reg, tmp_reg, swap_reg);
#else
  orr(tmp_reg, tmp_reg, TREG);
#endif
  ld_ptr(swap_reg, saved_mark_addr);

  cmpxchg(Address(obj_reg, 0), swap_reg, tmp_reg, AT, false, false);
  if (need_tmp_reg) {
    pop(tmp_reg);
  }
  // If the biasing toward our thread failed, then another thread
  // succeeded in biasing it toward itself and we need to revoke that
  // bias. The revocation will occur in the runtime in the slow case.
  if (PrintBiasedLockingStatistics) {
    Label L;
    bne(AT, R0, L);
    push(AT);
    push(tmp_reg);
    atomic_inc32((address)BiasedLocking::rebiased_lock_entry_count_addr(), 1, AT, tmp_reg);
    pop(tmp_reg);
    pop(AT);
    bind(L);
  }
  if (slow_case != NULL) {
    beq_far(AT, R0, *slow_case);
  }

  b(done);
  bind(try_revoke_bias);
  // The prototype mark in the klass doesn't have the bias bit set any
  // more, indicating that objects of this data type are not supposed
  // to be biased any more. We are going to try to reset the mark of
  // this object to the prototype value and fall through to the
  // CAS-based locking scheme. Note that if our CAS fails, it means
  // that another thread raced us for the privilege of revoking the
  // bias of this particular object, so it's okay to continue in the
  // normal locking code.
  //
  // FIXME: due to a lack of registers we currently blow away the age
  // bits in this situation. Should attempt to preserve them.
  ld_ptr(swap_reg, saved_mark_addr);

  if (need_tmp_reg) {
    push(tmp_reg);
  }
  load_prototype_header(tmp_reg, obj_reg);
  cmpxchg(Address(obj_reg, 0), swap_reg, tmp_reg, AT, false, false);
  if (need_tmp_reg) {
    pop(tmp_reg);
  }
  // Fall through to the normal CAS-based lock, because no matter what
  // the result of the above CAS, some thread must have succeeded in
  // removing the bias bit from the object's header.
  if (PrintBiasedLockingStatistics) {
    Label L;
    bne(AT, R0, L);
    push(AT);
    push(tmp_reg);
    atomic_inc32((address)BiasedLocking::revoked_lock_entry_count_addr(), 1, AT, tmp_reg);
    pop(tmp_reg);
    pop(AT);
    bind(L);
  }

  bind(cas_label);
  return null_check_offset;
}

void MacroAssembler::biased_locking_exit(Register obj_reg, Register temp_reg, Label& done) {
  assert(UseBiasedLocking, "why call this otherwise?");

  // Check for biased locking unlock case, which is a no-op
  // Note: we do not have to check the thread ID for two reasons.
  // First, the interpreter checks for IllegalMonitorStateException at
  // a higher level. Second, if the bias was revoked while we held the
  // lock, the object could not be rebiased toward another thread, so
  // the bias bit would be clear.
  ld_d(temp_reg, Address(obj_reg, oopDesc::mark_offset_in_bytes()));
  andi(temp_reg, temp_reg, markOopDesc::biased_lock_mask_in_place);
  addi_d(AT, R0, markOopDesc::biased_lock_pattern);

  beq(AT, temp_reg, done);
}

// the stack pointer adjustment is needed. see InterpreterMacroAssembler::super_call_VM_leaf
// this method will handle the stack problem, you need not to preserve the stack space for the argument now
void MacroAssembler::call_VM_leaf_base(address entry_point, int number_of_arguments) {
  Label L, E;

  assert(number_of_arguments <= 4, "just check");

  andi(AT, SP, 0xf);
  beq(AT, R0, L);
  addi_d(SP, SP, -8);
  call(entry_point, relocInfo::runtime_call_type);
  addi_d(SP, SP, 8);
  b(E);

  bind(L);
  call(entry_point, relocInfo::runtime_call_type);
  bind(E);
}

void MacroAssembler::jmp(address entry) {
  jlong offs = entry - pc();
  if (reachable_from_branch_short(offs)) { // Short jump
    b(offset26(entry));
  } else {                                 // Far jump
    patchable_jump_far(R0, offs);
  }
}

void MacroAssembler::jmp(address entry, relocInfo::relocType rtype) {
  switch (rtype) {
    case relocInfo::none:
      jmp(entry);
      break;
    default:
      {
        InstructionMark im(this);
        relocate(rtype);
        patchable_jump(entry);
      }
      break;
  }
}

void MacroAssembler::jmp_far(Label& L) {
  if (L.is_bound()) {
    assert(target(L) != NULL, "jmp most probably wrong");
    patchable_jump(target(L), true /* force patchable */);
  } else {
    L.add_patch_at(code(), locator());
    patchable_jump_far(R0, 0);
  }
}

void MacroAssembler::mov_metadata(Address dst, Metadata* obj) {
  int oop_index;
  if (obj) {
    oop_index = oop_recorder()->find_index(obj);
  } else {
    oop_index = oop_recorder()->allocate_metadata_index(obj);
  }
  relocate(metadata_Relocation::spec(oop_index));
  patchable_li52(AT, (long)obj);
  st_d(AT, dst);
}

void MacroAssembler::mov_metadata(Register dst, Metadata* obj) {
  int oop_index;
  if (obj) {
    oop_index = oop_recorder()->find_index(obj);
  } else {
    oop_index = oop_recorder()->allocate_metadata_index(obj);
  }
  relocate(metadata_Relocation::spec(oop_index));
  patchable_li52(dst, (long)obj);
}

void MacroAssembler::call(address entry) {
  jlong offs = entry - pc();
  if (reachable_from_branch_short(offs)) { // Short call (pc-rel)
    bl(offset26(entry));
  } else if (is_simm(offs, 38)) {          // Far call (pc-rel)
    patchable_jump_far(RA, offs);
  } else {                                 // Long call (absolute)
    call_long(entry);
  }
}

void MacroAssembler::call(address entry, relocInfo::relocType rtype) {
  switch (rtype) {
    case relocInfo::none:
      call(entry);
      break;
    case relocInfo::runtime_call_type:
      if (!is_simm(entry - pc(), 38)) {
        call_long(entry);
        break;
      }
      // fallthrough
    default:
      {
        InstructionMark im(this);
        relocate(rtype);
        patchable_call(entry);
      }
      break;
  }
}

void MacroAssembler::call(address entry, RelocationHolder& rh){
  switch (rh.type()) {
    case relocInfo::none:
      call(entry);
      break;
    case relocInfo::runtime_call_type:
      if (!is_simm(entry - pc(), 38)) {
        call_long(entry);
        break;
      }
      // fallthrough
    default:
      {
        InstructionMark im(this);
        relocate(rh);
        patchable_call(entry);
      }
      break;
  }
}

void MacroAssembler::call_long(address entry) {
  jlong value = (jlong)entry;
  lu12i_w(T4, split_low20(value >> 12));
  lu32i_d(T4, split_low20(value >> 32));
  jirl(RA, T4, split_low12(value));
}

address MacroAssembler::ic_call(address entry, jint method_index) {
  RelocationHolder rh = virtual_call_Relocation::spec(pc(), method_index);
  patchable_li52(IC_Klass, (long)Universe::non_oop_word());
  assert(entry != NULL, "call most probably wrong");
  InstructionMark im(this);
  return trampoline_call(AddressLiteral(entry, rh));
}

void MacroAssembler::c2bool(Register r) {
  sltu(r, R0, r);
}

#ifndef PRODUCT
extern "C" void findpc(intptr_t x);
#endif

void MacroAssembler::debug(char* msg/*, RegistersForDebugging* regs*/) {
  if ( ShowMessageBoxOnError ) {
    JavaThreadState saved_state = JavaThread::current()->thread_state();
    JavaThread::current()->set_thread_state(_thread_in_vm);
    {
      // In order to get locks work, we need to fake a in_VM state
      ttyLocker ttyl;
      ::tty->print_cr("EXECUTION STOPPED: %s\n", msg);
      if (CountBytecodes || TraceBytecodes || StopInterpreterAt) {
  BytecodeCounter::print();
      }

    }
    ThreadStateTransition::transition(JavaThread::current(), _thread_in_vm, saved_state);
  }
  else
    ::tty->print_cr("=============== DEBUG MESSAGE: %s ================\n", msg);
}


void MacroAssembler::stop(const char* msg) {
  li(A0, (long)msg);
  call(CAST_FROM_FN_PTR(address, MacroAssembler::debug), relocInfo::runtime_call_type);
  brk(17);
}

void MacroAssembler::warn(const char* msg) {
  pushad();
  li(A0, (long)msg);
  push(S2);
  li(AT, -(StackAlignmentInBytes));
  move(S2, SP);     // use S2 as a sender SP holder
  andr(SP, SP, AT); // align stack as required by ABI
  call(CAST_FROM_FN_PTR(address, MacroAssembler::debug), relocInfo::runtime_call_type);
  move(SP, S2);     // use S2 as a sender SP holder
  pop(S2);
  popad();
}

void MacroAssembler::increment(Register reg, int imm) {
  if (!imm) return;
  if (is_simm(imm, 12)) {
    addi_d(reg, reg, imm);
  } else {
    li(AT, imm);
    add_d(reg, reg, AT);
  }
}

void MacroAssembler::decrement(Register reg, int imm) {
  increment(reg, -imm);
}

void MacroAssembler::increment(Address addr, int imm) {
  if (!imm) return;
  assert(is_simm(imm, 12), "must be");
  ld_ptr(AT, addr);
  addi_d(AT, AT, imm);
  st_ptr(AT, addr);
}

void MacroAssembler::decrement(Address addr, int imm) {
  increment(addr, -imm);
}

void MacroAssembler::call_VM(Register oop_result,
                             address entry_point,
                             bool check_exceptions) {
  call_VM_helper(oop_result, entry_point, 0, check_exceptions);
}

void MacroAssembler::call_VM(Register oop_result,
                             address entry_point,
                             Register arg_1,
                             bool check_exceptions) {
  if (arg_1!=A1) move(A1, arg_1);
  call_VM_helper(oop_result, entry_point, 1, check_exceptions);
}

void MacroAssembler::call_VM(Register oop_result,
                             address entry_point,
                             Register arg_1,
                             Register arg_2,
                             bool check_exceptions) {
  if (arg_1!=A1) move(A1, arg_1);
  if (arg_2!=A2) move(A2, arg_2);
  assert(arg_2 != A1, "smashed argument");
  call_VM_helper(oop_result, entry_point, 2, check_exceptions);
}

void MacroAssembler::call_VM(Register oop_result,
                             address entry_point,
                             Register arg_1,
                             Register arg_2,
                             Register arg_3,
                             bool check_exceptions) {
  if (arg_1!=A1) move(A1, arg_1);
  if (arg_2!=A2) move(A2, arg_2); assert(arg_2 != A1, "smashed argument");
  if (arg_3!=A3) move(A3, arg_3); assert(arg_3 != A1 && arg_3 != A2, "smashed argument");
  call_VM_helper(oop_result, entry_point, 3, check_exceptions);
}

void MacroAssembler::call_VM(Register oop_result,
                             Register last_java_sp,
                             address entry_point,
                             int number_of_arguments,
                             bool check_exceptions) {
  call_VM_base(oop_result, NOREG, last_java_sp, entry_point, number_of_arguments, check_exceptions);
}

void MacroAssembler::call_VM(Register oop_result,
                             Register last_java_sp,
                             address entry_point,
                             Register arg_1,
                             bool check_exceptions) {
  if (arg_1 != A1) move(A1, arg_1);
  call_VM(oop_result, last_java_sp, entry_point, 1, check_exceptions);
}

void MacroAssembler::call_VM(Register oop_result,
                             Register last_java_sp,
                             address entry_point,
                             Register arg_1,
                             Register arg_2,
                             bool check_exceptions) {
  if (arg_1 != A1) move(A1, arg_1);
  if (arg_2 != A2) move(A2, arg_2); assert(arg_2 != A1, "smashed argument");
  call_VM(oop_result, last_java_sp, entry_point, 2, check_exceptions);
}

void MacroAssembler::call_VM(Register oop_result,
                             Register last_java_sp,
                             address entry_point,
                             Register arg_1,
                             Register arg_2,
                             Register arg_3,
                             bool check_exceptions) {
  if (arg_1 != A1) move(A1, arg_1);
  if (arg_2 != A2) move(A2, arg_2); assert(arg_2 != A1, "smashed argument");
  if (arg_3 != A3) move(A3, arg_3); assert(arg_3 != A1 && arg_3 != A2, "smashed argument");
  call_VM(oop_result, last_java_sp, entry_point, 3, check_exceptions);
}

void MacroAssembler::call_VM_base(Register oop_result,
                                  Register java_thread,
                                  Register last_java_sp,
                                  address  entry_point,
                                  int      number_of_arguments,
                                  bool     check_exceptions) {
  // determine java_thread register
  if (!java_thread->is_valid()) {
#ifndef OPT_THREAD
    java_thread = T2;
    get_thread(java_thread);
#else
    java_thread = TREG;
#endif
  }
  // determine last_java_sp register
  if (!last_java_sp->is_valid()) {
    last_java_sp = SP;
  }
  // debugging support
  assert(number_of_arguments >= 0   , "cannot have negative number of arguments");
  assert(number_of_arguments <= 4   , "cannot have negative number of arguments");
  assert(java_thread != oop_result  , "cannot use the same register for java_thread & oop_result");
  assert(java_thread != last_java_sp, "cannot use the same register for java_thread & last_java_sp");

  assert(last_java_sp != FP, "this code doesn't work for last_java_sp == fp, which currently can't portably work anyway since C2 doesn't save fp");

  // set last Java frame before call
  Label before_call;
  bind(before_call);
  set_last_Java_frame(java_thread, last_java_sp, FP, before_call);

  // do the call
  move(A0, java_thread);
  call(entry_point, relocInfo::runtime_call_type);

  // restore the thread (cannot use the pushed argument since arguments
  // may be overwritten by C code generated by an optimizing compiler);
  // however can use the register value directly if it is callee saved.
#ifndef OPT_THREAD
  get_thread(java_thread);
#else
#ifdef ASSERT
  {
    Label L;
    get_thread(AT);
    beq(java_thread, AT, L);
    stop("MacroAssembler::call_VM_base: TREG not callee saved?");
    bind(L);
  }
#endif
#endif

  // discard thread and arguments
  ld_ptr(SP, java_thread, in_bytes(JavaThread::last_Java_sp_offset()));
  // reset last Java frame
  reset_last_Java_frame(java_thread, false);

  check_and_handle_popframe(java_thread);
  check_and_handle_earlyret(java_thread);
  if (check_exceptions) {
    // check for pending exceptions (java_thread is set upon return)
    Label L;
    ld_d(AT, java_thread, in_bytes(Thread::pending_exception_offset()));
    beq(AT, R0, L);
    li(AT, target(before_call));
    push(AT);
    jmp(StubRoutines::forward_exception_entry(), relocInfo::runtime_call_type);
    bind(L);
  }

  // get oop result if there is one and reset the value in the thread
  if (oop_result->is_valid()) {
    ld_d(oop_result, java_thread, in_bytes(JavaThread::vm_result_offset()));
    st_d(R0, java_thread, in_bytes(JavaThread::vm_result_offset()));
    verify_oop(oop_result);
  }
}

void MacroAssembler::call_VM_helper(Register oop_result, address entry_point, int number_of_arguments, bool check_exceptions) {
  move(V0, SP);
  //we also reserve space for java_thread here
  li(AT, -(StackAlignmentInBytes));
  andr(SP, SP, AT);
  call_VM_base(oop_result, NOREG, V0, entry_point, number_of_arguments, check_exceptions);
}

void MacroAssembler::call_VM_leaf(address entry_point, int number_of_arguments) {
  call_VM_leaf_base(entry_point, number_of_arguments);
}

void MacroAssembler::call_VM_leaf(address entry_point, Register arg_0) {
  if (arg_0 != A0) move(A0, arg_0);
  call_VM_leaf(entry_point, 1);
}

void MacroAssembler::call_VM_leaf(address entry_point, Register arg_0, Register arg_1) {
  if (arg_0 != A0) move(A0, arg_0);
  if (arg_1 != A1) move(A1, arg_1); assert(arg_1 != A0, "smashed argument");
  call_VM_leaf(entry_point, 2);
}

void MacroAssembler::call_VM_leaf(address entry_point, Register arg_0, Register arg_1, Register arg_2) {
  if (arg_0 != A0) move(A0, arg_0);
  if (arg_1 != A1) move(A1, arg_1); assert(arg_1 != A0, "smashed argument");
  if (arg_2 != A2) move(A2, arg_2); assert(arg_2 != A0 && arg_2 != A1, "smashed argument");
  call_VM_leaf(entry_point, 3);
}

void MacroAssembler::super_call_VM_leaf(address entry_point) {
  MacroAssembler::call_VM_leaf_base(entry_point, 0);
}

void MacroAssembler::super_call_VM_leaf(address entry_point,
                                                   Register arg_1) {
  if (arg_1 != A0) move(A0, arg_1);
  MacroAssembler::call_VM_leaf_base(entry_point, 1);
}

void MacroAssembler::super_call_VM_leaf(address entry_point,
                                                   Register arg_1,
                                                   Register arg_2) {
  if (arg_1 != A0) move(A0, arg_1);
  if (arg_2 != A1) move(A1, arg_2); assert(arg_2 != A0, "smashed argument");
  MacroAssembler::call_VM_leaf_base(entry_point, 2);
}

void MacroAssembler::super_call_VM_leaf(address entry_point,
                                                   Register arg_1,
                                                   Register arg_2,
                                                   Register arg_3) {
  if (arg_1 != A0) move(A0, arg_1);
  if (arg_2 != A1) move(A1, arg_2); assert(arg_2 != A0, "smashed argument");
  if (arg_3 != A2) move(A2, arg_3); assert(arg_3 != A0 && arg_3 != A1, "smashed argument");
  MacroAssembler::call_VM_leaf_base(entry_point, 3);
}

void MacroAssembler::check_and_handle_earlyret(Register java_thread) {
}

void MacroAssembler::check_and_handle_popframe(Register java_thread) {
}

void MacroAssembler::null_check(Register reg, int offset) {
  if (needs_explicit_null_check(offset)) {
    // provoke OS NULL exception if reg = NULL by
    // accessing M[reg] w/o changing any (non-CC) registers
    // NOTE: cmpl is plenty here to provoke a segv
    ld_w(AT, reg, 0);
  } else {
    // nothing to do, (later) access of M[reg + offset]
    // will provoke OS NULL exception if reg = NULL
  }
}

void MacroAssembler::enter() {
  push2(RA, FP);
  move(FP, SP);
}

void MacroAssembler::leave() {
  move(SP, FP);
  pop2(RA, FP);
}

void MacroAssembler::build_frame(int framesize) {
  assert(framesize >= 2 * wordSize, "framesize must include space for FP/RA");
  assert(framesize % (2 * wordSize) == 0, "must preserve 2 * wordSize alignment");
  if (Assembler::is_simm(-framesize, 12)) {
    addi_d(SP, SP, -framesize);
    st_ptr(FP, Address(SP, framesize - 2 * wordSize));
    st_ptr(RA, Address(SP, framesize - 1 * wordSize));
    if (PreserveFramePointer)
      addi_d(FP, SP, framesize - 2 * wordSize);
  } else {
    addi_d(SP, SP, -2 * wordSize);
    st_ptr(FP, Address(SP, 0 * wordSize));
    st_ptr(RA, Address(SP, 1 * wordSize));
    if (PreserveFramePointer)
      move(FP, SP);
    li(SCR1, framesize - 2 * wordSize);
    sub_d(SP, SP, SCR1);
  }
}

void MacroAssembler::remove_frame(int framesize) {
  assert(framesize >= 2 * wordSize, "framesize must include space for FP/RA");
  assert(framesize % (2*wordSize) == 0, "must preserve 2*wordSize alignment");
  if (Assembler::is_simm(framesize, 12)) {
    ld_ptr(FP, Address(SP, framesize - 2 * wordSize));
    ld_ptr(RA, Address(SP, framesize - 1 * wordSize));
    addi_d(SP, SP, framesize);
  } else {
    li(SCR1, framesize - 2 * wordSize);
    add_d(SP, SP, SCR1);
    ld_ptr(FP, Address(SP, 0 * wordSize));
    ld_ptr(RA, Address(SP, 1 * wordSize));
    addi_d(SP, SP, 2 * wordSize);
  }
}

void MacroAssembler::unimplemented(const char* what) {
  const char* buf = NULL;
  {
    ResourceMark rm;
    stringStream ss;
    ss.print("unimplemented: %s", what);
    buf = code_string(ss.as_string());
  }
  stop(buf);
}

void MacroAssembler::get_thread(Register thread) {
#ifdef MINIMIZE_RAM_USAGE
  Register tmp;

  if (thread == AT)
    tmp = T4;
  else
    tmp = AT;

  move(thread, SP);
  shr(thread, PAGE_SHIFT);

  push(tmp);
  li(tmp, ((1UL << (SP_BITLENGTH - PAGE_SHIFT)) - 1));
  andr(thread, thread, tmp);
  shl(thread, Address::times_ptr); // sizeof(Thread *)
  li(tmp, (long)ThreadLocalStorage::sp_map_addr());
  add_d(tmp, tmp, thread);
  ld_ptr(thread, tmp, 0);
  pop(tmp);
#else
  if (thread != V0) {
    push(V0);
  }
  pushad_except_v0();

  push(S5);
  move(S5, SP);
  li(AT, -StackAlignmentInBytes);
  andr(SP, SP, AT);
  // TODO: confirm reloc
  call(CAST_FROM_FN_PTR(address, Thread::current), relocInfo::runtime_call_type);
  move(SP, S5);
  pop(S5);

  popad_except_v0();
  if (thread != V0) {
    move(thread, V0);
    pop(V0);
  }
#endif // MINIMIZE_RAM_USAGE
}

void MacroAssembler::reset_last_Java_frame(Register java_thread, bool clear_fp) {
  // determine java_thread register
  if (!java_thread->is_valid()) {
#ifndef OPT_THREAD
    java_thread = T1;
    get_thread(java_thread);
#else
    java_thread = TREG;
#endif
  }
  // we must set sp to zero to clear frame
  st_ptr(R0, java_thread, in_bytes(JavaThread::last_Java_sp_offset()));
  // must clear fp, so that compiled frames are not confused; it is possible
  // that we need it only for debugging
  if(clear_fp) {
    st_ptr(R0, java_thread, in_bytes(JavaThread::last_Java_fp_offset()));
  }

  // Always clear the pc because it could have been set by make_walkable()
  st_ptr(R0, java_thread, in_bytes(JavaThread::last_Java_pc_offset()));
}

void MacroAssembler::reset_last_Java_frame(bool clear_fp) {
  Register thread = TREG;
#ifndef OPT_THREAD
  get_thread(thread);
#endif
  // we must set sp to zero to clear frame
  st_d(R0, Address(thread, JavaThread::last_Java_sp_offset()));
  // must clear fp, so that compiled frames are not confused; it is
  // possible that we need it only for debugging
  if (clear_fp) {
    st_d(R0, Address(thread, JavaThread::last_Java_fp_offset()));
  }

  // Always clear the pc because it could have been set by make_walkable()
  st_d(R0, Address(thread, JavaThread::last_Java_pc_offset()));
}

// Write serialization page so VM thread can do a pseudo remote membar.
// We use the current thread pointer to calculate a thread specific
// offset to write to within the page. This minimizes bus traffic
// due to cache line collision.
void MacroAssembler::serialize_memory(Register thread, Register tmp) {
  assert_different_registers(AT, tmp);
  juint sps = os::get_serialize_page_shift_count();
  juint lsb = sps + 2;
  juint msb = sps + log2_uint(os::vm_page_size()) - 1;
  bstrpick_w(AT, thread, msb, lsb);
  li(tmp, os::get_memory_serialize_page());
  alsl_d(tmp, AT, tmp, Address::times_2 - 1);
  st_w(R0, tmp, 0);
}

void MacroAssembler::safepoint_poll(Label& slow_path, Register thread_reg) {
  if (SafepointMechanism::uses_thread_local_poll()) {
    ld_d(AT, thread_reg, in_bytes(Thread::polling_page_offset()));
    andi(AT, AT, SafepointMechanism::poll_bit());
    bne(AT, R0, slow_path);
  } else {
    li(AT, SafepointSynchronize::address_of_state());
    ld_w(AT, AT, 0);
    addi_d(AT, AT, -SafepointSynchronize::_not_synchronized);
    bne(AT, R0, slow_path);
  }
}

// Just like safepoint_poll, but use an acquiring load for thread-
// local polling.
//
// We need an acquire here to ensure that any subsequent load of the
// global SafepointSynchronize::_state flag is ordered after this load
// of the local Thread::_polling page.  We don't want this poll to
// return false (i.e. not safepointing) and a later poll of the global
// SafepointSynchronize::_state spuriously to return true.
//
// This is to avoid a race when we're in a native->Java transition
// racing the code which wakes up from a safepoint.
//
void MacroAssembler::safepoint_poll_acquire(Label& slow_path, Register thread_reg) {
  if (SafepointMechanism::uses_thread_local_poll()) {
    ld_d(AT, thread_reg, in_bytes(Thread::polling_page_offset()));
    membar(Assembler::Membar_mask_bits(LoadLoad|LoadStore));
    andi(AT, AT, SafepointMechanism::poll_bit());
    bne(AT, R0, slow_path);
  } else {
    safepoint_poll(slow_path, thread_reg);
  }
}

// Calls to C land
//
// When entering C land, the fp, & sp of the last Java frame have to be recorded
// in the (thread-local) JavaThread object. When leaving C land, the last Java fp
// has to be reset to 0. This is required to allow proper stack traversal.
void MacroAssembler::set_last_Java_frame(Register java_thread,
                                         Register last_java_sp,
                                         Register last_java_fp,
                                         Label& last_java_pc) {
  // determine java_thread register
  if (!java_thread->is_valid()) {
#ifndef OPT_THREAD
    java_thread = T2;
    get_thread(java_thread);
#else
    java_thread = TREG;
#endif
  }

  // determine last_java_sp register
  if (!last_java_sp->is_valid()) {
    last_java_sp = SP;
  }

  // last_java_fp is optional
  if (last_java_fp->is_valid()) {
    st_ptr(last_java_fp, java_thread, in_bytes(JavaThread::last_Java_fp_offset()));
  }

  // last_java_pc
  lipc(AT, last_java_pc);
  st_ptr(AT, java_thread, in_bytes(JavaThread::frame_anchor_offset() +
                                   JavaFrameAnchor::last_Java_pc_offset()));

  st_ptr(last_java_sp, java_thread, in_bytes(JavaThread::last_Java_sp_offset()));
}

void MacroAssembler::set_last_Java_frame(Register last_java_sp,
                                         Register last_java_fp,
                                         Label& last_java_pc) {
  set_last_Java_frame(NOREG, last_java_sp, last_java_fp, last_java_pc);
}

// Defines obj, preserves var_size_in_bytes, okay for t2 == var_size_in_bytes.
void MacroAssembler::tlab_allocate(Register obj,
                                   Register var_size_in_bytes,
                                   int con_size_in_bytes,
                                   Register t1,
                                   Register t2,
                                   Label& slow_case) {
  BarrierSetAssembler *bs = BarrierSet::barrier_set()->barrier_set_assembler();
  bs->tlab_allocate(this, obj, var_size_in_bytes, con_size_in_bytes, t1, t2, slow_case);
}

// Defines obj, preserves var_size_in_bytes
void MacroAssembler::eden_allocate(Register obj,
                                   Register var_size_in_bytes,
                                   int con_size_in_bytes,
                                   Register t1,
                                   Label& slow_case) {
  BarrierSetAssembler *bs = BarrierSet::barrier_set()->barrier_set_assembler();
  bs->eden_allocate(this, obj, var_size_in_bytes, con_size_in_bytes, t1, slow_case);
}


void MacroAssembler::incr_allocated_bytes(Register thread,
                                          Register var_size_in_bytes,
                                          int con_size_in_bytes,
                                          Register t1) {
  if (!thread->is_valid()) {
#ifndef OPT_THREAD
    assert(t1->is_valid(), "need temp reg");
    thread = t1;
    get_thread(thread);
#else
    thread = TREG;
#endif
  }

  ld_ptr(AT, thread, in_bytes(JavaThread::allocated_bytes_offset()));
  if (var_size_in_bytes->is_valid()) {
    add_d(AT, AT, var_size_in_bytes);
  } else {
    addi_d(AT, AT, con_size_in_bytes);
  }
  st_ptr(AT, thread, in_bytes(JavaThread::allocated_bytes_offset()));
}

void MacroAssembler::li(Register rd, jlong value) {
  jlong hi12 = bitfield(value, 52, 12);
  jlong lo52 = bitfield(value,  0, 52);

  if (hi12 != 0 && lo52 == 0) {
    lu52i_d(rd, R0, hi12);
  } else {
    jlong hi20 = bitfield(value, 32, 20);
    jlong lo20 = bitfield(value, 12, 20);
    jlong lo12 = bitfield(value,  0, 12);

    if (lo20 == 0) {
      ori(rd, R0, lo12);
    } else if (bitfield(simm12(lo12), 12, 20) == lo20) {
      addi_w(rd, R0, simm12(lo12));
    } else {
      lu12i_w(rd, lo20);
      if (lo12 != 0)
        ori(rd, rd, lo12);
    }
    if (hi20 != bitfield(simm20(lo20), 20, 20))
      lu32i_d(rd, hi20);
    if (hi12 != bitfield(simm20(hi20), 20, 12))
      lu52i_d(rd, rd, hi12);
  }
}

void MacroAssembler::patchable_li52(Register rd, jlong value) {
  int count = 0;

  if (value <= max_jint && value >= min_jint) {
    if (is_simm(value, 12)) {
      addi_d(rd, R0, value);
      count++;
    } else {
      lu12i_w(rd, split_low20(value >> 12));
      count++;
      if (split_low12(value)) {
        ori(rd, rd, split_low12(value));
        count++;
      }
    }
  } else if (is_simm(value, 52)) {
    lu12i_w(rd, split_low20(value >> 12));
    count++;
    if (split_low12(value)) {
      ori(rd, rd, split_low12(value));
      count++;
    }
    lu32i_d(rd, split_low20(value >> 32));
    count++;
  } else {
    tty->print_cr("value = 0x%lx", value);
    guarantee(false, "Not supported yet !");
  }

  while (count < 3) {
    nop();
    count++;
  }
}

void MacroAssembler::lipc(Register rd, Label& L) {
  if (L.is_bound()) {
    jint offs = (target(L) - pc()) >> 2;
    guarantee(is_simm(offs, 20), "Not signed 20-bit offset");
    pcaddi(rd, offs);
  } else {
    InstructionMark im(this);
    L.add_patch_at(code(), locator());
    pcaddi(rd, 0);
  }
}

void MacroAssembler::set_narrow_klass(Register dst, Klass* k) {
  assert(UseCompressedClassPointers, "should only be used for compressed header");
  assert(oop_recorder() != NULL, "this assembler needs an OopRecorder");

  int klass_index = oop_recorder()->find_index(k);
  RelocationHolder rspec = metadata_Relocation::spec(klass_index);
  long narrowKlass = (long)Klass::encode_klass(k);

  relocate(rspec, Assembler::narrow_oop_operand);
  patchable_li52(dst, narrowKlass);
}

void MacroAssembler::set_narrow_oop(Register dst, jobject obj) {
  assert(UseCompressedOops, "should only be used for compressed header");
  assert(oop_recorder() != NULL, "this assembler needs an OopRecorder");

  int oop_index = oop_recorder()->find_index(obj);
  RelocationHolder rspec = oop_Relocation::spec(oop_index);

  relocate(rspec, Assembler::narrow_oop_operand);
  patchable_li52(dst, oop_index);
}

// ((OopHandle)result).resolve();
void MacroAssembler::resolve_oop_handle(Register result, Register tmp) {
  // OopHandle::resolve is an indirection.
  access_load_at(T_OBJECT, IN_NATIVE, result, Address(result, 0), tmp, NOREG);
}

void MacroAssembler::load_mirror(Register mirror, Register method, Register tmp) {
  // get mirror
  const int mirror_offset = in_bytes(Klass::java_mirror_offset());
  ld_ptr(mirror, method, in_bytes(Method::const_offset()));
  ld_ptr(mirror, mirror, in_bytes(ConstMethod::constants_offset()));
  ld_ptr(mirror, mirror, ConstantPool::pool_holder_offset_in_bytes());
  ld_ptr(mirror, mirror, mirror_offset);
  resolve_oop_handle(mirror, tmp);
}

void MacroAssembler::verify_oop(Register reg, const char* s) {
  if (!VerifyOops) return;

  const char * b = NULL;
  stringStream ss;
  ss.print("verify_oop: %s: %s", reg->name(), s);
  b = code_string(ss.as_string());

  addi_d(SP, SP, -6 * wordSize);
  st_ptr(SCR1, Address(SP, 0 * wordSize));
  st_ptr(SCR2, Address(SP, 1 * wordSize));
  st_ptr(RA, Address(SP, 2 * wordSize));
  st_ptr(A0, Address(SP, 3 * wordSize));
  st_ptr(A1, Address(SP, 4 * wordSize));

  move(A1, reg);
  patchable_li52(A0, (uintptr_t)(address)b); // Fixed size instructions
  li(SCR2, StubRoutines::verify_oop_subroutine_entry_address());
  ld_ptr(SCR2, Address(SCR2));
  jalr(SCR2);

  ld_ptr(SCR1, Address(SP, 0 * wordSize));
  ld_ptr(SCR2, Address(SP, 1 * wordSize));
  ld_ptr(RA, Address(SP, 2 * wordSize));
  ld_ptr(A0, Address(SP, 3 * wordSize));
  ld_ptr(A1, Address(SP, 4 * wordSize));
  addi_d(SP, SP, 6 * wordSize);
}

void MacroAssembler::verify_oop_addr(Address addr, const char* s) {
  if (!VerifyOops) return;

  const char* b = NULL;
  {
    ResourceMark rm;
    stringStream ss;
    ss.print("verify_oop_addr: %s", s);
    b = code_string(ss.as_string());
  }

  addi_d(SP, SP, -6 * wordSize);
  st_ptr(SCR1, Address(SP, 0 * wordSize));
  st_ptr(SCR2, Address(SP, 1 * wordSize));
  st_ptr(RA, Address(SP, 2 * wordSize));
  st_ptr(A0, Address(SP, 3 * wordSize));
  st_ptr(A1, Address(SP, 4 * wordSize));

  patchable_li52(A0, (uintptr_t)(address)b); // Fixed size instructions
  // addr may contain sp so we will have to adjust it based on the
  // pushes that we just did.
  if (addr.uses(SP)) {
    lea(A1, addr);
    ld_ptr(A1, Address(A1, 6 * wordSize));
  } else {
    ld_ptr(A1, addr);
  }

  // call indirectly to solve generation ordering problem
  li(SCR2, StubRoutines::verify_oop_subroutine_entry_address());
  ld_ptr(SCR2, Address(SCR2));
  jalr(SCR2);

  ld_ptr(SCR1, Address(SP, 0 * wordSize));
  ld_ptr(SCR2, Address(SP, 1 * wordSize));
  ld_ptr(RA, Address(SP, 2 * wordSize));
  ld_ptr(A0, Address(SP, 3 * wordSize));
  ld_ptr(A1, Address(SP, 4 * wordSize));
  addi_d(SP, SP, 6 * wordSize);
}

// used registers :  SCR1, SCR2
void MacroAssembler::verify_oop_subroutine() {
  // RA: ra
  // A0: char* error message
  // A1: oop   object to verify
  Label exit, error;
  // increment counter
  li(SCR2, (long)StubRoutines::verify_oop_count_addr());
  ld_w(SCR1, SCR2, 0);
  addi_d(SCR1, SCR1, 1);
  st_w(SCR1, SCR2, 0);

  // make sure object is 'reasonable'
  beqz(A1, exit);         // if obj is NULL it is ok

  // Check if the oop is in the right area of memory
  // const int oop_mask = Universe::verify_oop_mask();
  // const int oop_bits = Universe::verify_oop_bits();
  const uintptr_t oop_mask = Universe::verify_oop_mask();
  const uintptr_t oop_bits = Universe::verify_oop_bits();
  li(SCR1, oop_mask);
  andr(SCR2, A1, SCR1);
  li(SCR1, oop_bits);
  bne(SCR2, SCR1, error);

  // make sure klass is 'reasonable'
  // add for compressedoops
  load_klass(SCR2, A1);
  beqz(SCR2, error);                        // if klass is NULL it is broken
  // return if everything seems ok
  bind(exit);

  jr(RA);

  // handle errors
  bind(error);
  pushad();
  call(CAST_FROM_FN_PTR(address, MacroAssembler::debug), relocInfo::runtime_call_type);
  popad();
  jr(RA);
}

void MacroAssembler::verify_tlab(Register t1, Register t2) {
#ifdef ASSERT
  assert_different_registers(t1, t2, AT);
  if (UseTLAB && VerifyOops) {
    Label next, ok;

    get_thread(t1);

    ld_ptr(t2, t1, in_bytes(JavaThread::tlab_top_offset()));
    ld_ptr(AT, t1, in_bytes(JavaThread::tlab_start_offset()));
    bgeu(t2, AT, next);

    stop("assert(top >= start)");

    bind(next);
    ld_ptr(AT, t1, in_bytes(JavaThread::tlab_end_offset()));
    bgeu(AT, t2, ok);

    stop("assert(top <= end)");

    bind(ok);

  }
#endif
}

RegisterOrConstant MacroAssembler::delayed_value_impl(intptr_t* delayed_value_addr,
                                                      Register tmp,
                                                      int offset) {
  //TODO: LA
  guarantee(0, "LA not implemented yet");
  return RegisterOrConstant(tmp);
}

void MacroAssembler::hswap(Register reg) {
  //short
  //andi(reg, reg, 0xffff);
  srli_w(AT, reg, 8);
  slli_w(reg, reg, 24);
  srai_w(reg, reg, 16);
  orr(reg, reg, AT);
}

void MacroAssembler::huswap(Register reg) {
  srli_d(AT, reg, 8);
  slli_d(reg, reg, 24);
  srli_d(reg, reg, 16);
  orr(reg, reg, AT);
  bstrpick_d(reg, reg, 15, 0);
}

// something funny to do this will only one more register AT
// 32 bits
void MacroAssembler::swap(Register reg) {
  srli_w(AT, reg, 8);
  slli_w(reg, reg, 24);
  orr(reg, reg, AT);
  //reg : 4 1 2 3
  srli_w(AT, AT, 16);
  xorr(AT, AT, reg);
  andi(AT, AT, 0xff);
  //AT : 0 0 0 1^3);
  xorr(reg, reg, AT);
  //reg : 4 1 2 1
  slli_w(AT, AT, 16);
  xorr(reg, reg, AT);
  //reg : 4 3 2 1
}

void MacroAssembler::cmpxchg(Address addr, Register oldval, Register newval,
                             Register resflag, bool retold, bool barrier) {
  assert(oldval != resflag, "oldval != resflag");
  assert(newval != resflag, "newval != resflag");
  Label again, succ, fail;

  bind(again);
  ll_d(resflag, addr);
  bne(resflag, oldval, fail);
  move(resflag, newval);
  sc_d(resflag, addr);
  beqz(resflag, again);
  b(succ);

  bind(fail);
  if (barrier)
    membar(LoadLoad);
  if (retold && oldval != R0)
    move(oldval, resflag);
  move(resflag, R0);
  bind(succ);
}

void MacroAssembler::cmpxchg(Address addr, Register oldval, Register newval,
                             Register tmp, bool retold, bool barrier, Label& succ, Label* fail) {
  assert(oldval != tmp, "oldval != tmp");
  assert(newval != tmp, "newval != tmp");
  Label again, neq;

  bind(again);
  ll_d(tmp, addr);
  bne(tmp, oldval, neq);
  move(tmp, newval);
  sc_d(tmp, addr);
  beqz(tmp, again);
  b(succ);

  bind(neq);
  if (barrier)
    membar(LoadLoad);
  if (retold && oldval != R0)
    move(oldval, tmp);
  if (fail)
    b(*fail);
}

void MacroAssembler::cmpxchg32(Address addr, Register oldval, Register newval,
                               Register resflag, bool sign, bool retold, bool barrier) {
  assert(oldval != resflag, "oldval != resflag");
  assert(newval != resflag, "newval != resflag");
  Label again, succ, fail;

  bind(again);
  ll_w(resflag, addr);
  if (!sign)
    lu32i_d(resflag, 0);
  bne(resflag, oldval, fail);
  move(resflag, newval);
  sc_w(resflag, addr);
  beqz(resflag, again);
  b(succ);

  bind(fail);
  if (barrier)
    membar(LoadLoad);
  if (retold && oldval != R0)
    move(oldval, resflag);
  move(resflag, R0);
  bind(succ);
}

void MacroAssembler::cmpxchg32(Address addr, Register oldval, Register newval, Register tmp,
                               bool sign, bool retold, bool barrier, Label& succ, Label* fail) {
  assert(oldval != tmp, "oldval != tmp");
  assert(newval != tmp, "newval != tmp");
  Label again, neq;

  bind(again);
  ll_w(tmp, addr);
  if (!sign)
    lu32i_d(tmp, 0);
  bne(tmp, oldval, neq);
  move(tmp, newval);
  sc_w(tmp, addr);
  beqz(tmp, again);
  b(succ);

  bind(neq);
  if (barrier)
    membar(LoadLoad);
  if (retold && oldval != R0)
    move(oldval, tmp);
  if (fail)
    b(*fail);
}

// be sure the three register is different
void MacroAssembler::rem_s(FloatRegister fd, FloatRegister fs, FloatRegister ft, FloatRegister tmp) {
  //TODO: LA
  guarantee(0, "LA not implemented yet");
}

// be sure the three register is different
void MacroAssembler::rem_d(FloatRegister fd, FloatRegister fs, FloatRegister ft, FloatRegister tmp) {
  //TODO: LA
  guarantee(0, "LA not implemented yet");
}

#ifdef COMPILER2
// Fast_Lock and Fast_Unlock used by C2

// Because the transitions from emitted code to the runtime
// monitorenter/exit helper stubs are so slow it's critical that
// we inline both the stack-locking fast-path and the inflated fast path.
//
// See also: cmpFastLock and cmpFastUnlock.
//
// What follows is a specialized inline transliteration of the code
// in slow_enter() and slow_exit().  If we're concerned about I$ bloat
// another option would be to emit TrySlowEnter and TrySlowExit methods
// at startup-time.  These methods would accept arguments as
// (Obj, Self, box, Scratch) and return success-failure
// indications in the icc.ZFlag.  Fast_Lock and Fast_Unlock would simply
// marshal the arguments and emit calls to TrySlowEnter and TrySlowExit.
// In practice, however, the # of lock sites is bounded and is usually small.
// Besides the call overhead, TrySlowEnter and TrySlowExit might suffer
// if the processor uses simple bimodal branch predictors keyed by EIP
// Since the helper routines would be called from multiple synchronization
// sites.
//
// An even better approach would be write "MonitorEnter()" and "MonitorExit()"
// in java - using j.u.c and unsafe - and just bind the lock and unlock sites
// to those specialized methods.  That'd give us a mostly platform-independent
// implementation that the JITs could optimize and inline at their pleasure.
// Done correctly, the only time we'd need to cross to native could would be
// to park() or unpark() threads.  We'd also need a few more unsafe operators
// to (a) prevent compiler-JIT reordering of non-volatile accesses, and
// (b) explicit barriers or fence operations.
//
// TODO:
//
// *  Arrange for C2 to pass "Self" into Fast_Lock and Fast_Unlock in one of the registers (scr).
//    This avoids manifesting the Self pointer in the Fast_Lock and Fast_Unlock terminals.
//    Given TLAB allocation, Self is usually manifested in a register, so passing it into
//    the lock operators would typically be faster than reifying Self.
//
// *  Ideally I'd define the primitives as:
//       fast_lock   (nax Obj, nax box, res, tmp, nax scr) where tmp and scr are KILLED.
//       fast_unlock (nax Obj, box, res, nax tmp) where tmp are KILLED
//    Unfortunately ADLC bugs prevent us from expressing the ideal form.
//    Instead, we're stuck with a rather awkward and brittle register assignments below.
//    Furthermore the register assignments are overconstrained, possibly resulting in
//    sub-optimal code near the synchronization site.
//
// *  Eliminate the sp-proximity tests and just use "== Self" tests instead.
//    Alternately, use a better sp-proximity test.
//
// *  Currently ObjectMonitor._Owner can hold either an sp value or a (THREAD *) value.
//    Either one is sufficient to uniquely identify a thread.
//    TODO: eliminate use of sp in _owner and use get_thread(tr) instead.
//
// *  Intrinsify notify() and notifyAll() for the common cases where the
//    object is locked by the calling thread but the waitlist is empty.
//    avoid the expensive JNI call to JVM_Notify() and JVM_NotifyAll().
//
// *  use jccb and jmpb instead of jcc and jmp to improve code density.
//    But beware of excessive branch density on AMD Opterons.
//
// *  Both Fast_Lock and Fast_Unlock set the ICC.ZF to indicate success
//    or failure of the fast-path.  If the fast-path fails then we pass
//    control to the slow-path, typically in C.  In Fast_Lock and
//    Fast_Unlock we often branch to DONE_LABEL, just to find that C2
//    will emit a conditional branch immediately after the node.
//    So we have branches to branches and lots of ICC.ZF games.
//    Instead, it might be better to have C2 pass a "FailureLabel"
//    into Fast_Lock and Fast_Unlock.  In the case of success, control
//    will drop through the node.  ICC.ZF is undefined at exit.
//    In the case of failure, the node will branch directly to the
//    FailureLabel

// obj: object to lock
// box: on-stack box address (displaced header location)
// tmp: tmp -- KILLED
// scr: tmp -- KILLED
void MacroAssembler::fast_lock(Register objReg, Register boxReg, Register resReg,
                               Register tmpReg, Register scrReg) {
  Label IsInflated, DONE, DONE_SET;

  // Ensure the register assignents are disjoint
  guarantee(objReg != boxReg, "");
  guarantee(objReg != tmpReg, "");
  guarantee(objReg != scrReg, "");
  guarantee(boxReg != tmpReg, "");
  guarantee(boxReg != scrReg, "");

  block_comment("FastLock");

  if (PrintBiasedLockingStatistics) {
    atomic_inc32((address)BiasedLocking::total_entry_count_addr(), 1, tmpReg, scrReg);
  }

  if (EmitSync & 1) {
    move(AT, R0);
    return;
  } else
    if (EmitSync & 2) {
      Label DONE_LABEL ;
      if (UseBiasedLocking) {
        // Note: tmpReg maps to the swap_reg argument and scrReg to the tmp_reg argument.
        biased_locking_enter(boxReg, objReg, tmpReg, scrReg, false, DONE_LABEL, NULL);
      }

      ld_d(tmpReg, Address(objReg, 0)) ;          // fetch markword
      ori(tmpReg, tmpReg, 0x1);
      st_d(tmpReg, Address(boxReg, 0));           // Anticipate successful CAS

      cmpxchg(Address(objReg, 0), tmpReg, boxReg, scrReg, true, false, DONE_LABEL); // Updates tmpReg

      // Recursive locking
      sub_d(tmpReg, tmpReg, SP);
      li(AT, (7 - os::vm_page_size() ));
      andr(tmpReg, tmpReg, AT);
      st_d(tmpReg, Address(boxReg, 0));
      bind(DONE_LABEL) ;
    } else {
      // Possible cases that we'll encounter in fast_lock
      // ------------------------------------------------
      // * Inflated
      //    -- unlocked
      //    -- Locked
      //       = by self
      //       = by other
      // * biased
      //    -- by Self
      //    -- by other
      // * neutral
      // * stack-locked
      //    -- by self
      //       = sp-proximity test hits
      //       = sp-proximity test generates false-negative
      //    -- by other
      //

      // TODO: optimize away redundant LDs of obj->mark and improve the markword triage
      // order to reduce the number of conditional branches in the most common cases.
      // Beware -- there's a subtle invariant that fetch of the markword
      // at [FETCH], below, will never observe a biased encoding (*101b).
      // If this invariant is not held we risk exclusion (safety) failure.
      if (UseBiasedLocking && !UseOptoBiasInlining) {
        Label succ, fail;
        biased_locking_enter(boxReg, objReg, tmpReg, scrReg, false, succ, NULL);
        b(fail);
        bind(succ);
        li(resReg, 1);
        b(DONE);
        bind(fail);
      }

      ld_d(tmpReg, Address(objReg, 0)); //Fetch the markword of the object.
      andi(AT, tmpReg, markOopDesc::monitor_value);
      bnez(AT, IsInflated); // inflated vs stack-locked|neutral|bias

      // Attempt stack-locking ...
      ori(tmpReg, tmpReg, markOopDesc::unlocked_value);
      st_d(tmpReg, Address(boxReg, 0)); // Anticipate successful CAS

      if (PrintBiasedLockingStatistics) {
        Label SUCC, FAIL;
        cmpxchg(Address(objReg, 0), tmpReg, boxReg, scrReg, true, false, SUCC, &FAIL); // Updates tmpReg
        bind(SUCC);
        atomic_inc32((address)BiasedLocking::fast_path_entry_count_addr(), 1, AT, scrReg);
        li(resReg, 1);
        b(DONE);
        bind(FAIL);
      } else {
        // If cmpxchg is succ, then scrReg = 1
        cmpxchg(Address(objReg, 0), tmpReg, boxReg, scrReg, true, false, DONE_SET); // Updates tmpReg
      }

      // Recursive locking
      // The object is stack-locked: markword contains stack pointer to BasicLock.
      // Locked by current thread if difference with current SP is less than one page.
      sub_d(tmpReg, tmpReg, SP);
      li(AT, 7 - os::vm_page_size());
      andr(tmpReg, tmpReg, AT);
      st_d(tmpReg, Address(boxReg, 0));

      if (PrintBiasedLockingStatistics) {
        Label L;
        // tmpReg == 0 => BiasedLocking::_fast_path_entry_count++
        bnez(tmpReg, L);
        atomic_inc32((address)BiasedLocking::fast_path_entry_count_addr(), 1, AT, scrReg);
        bind(L);
      }

      sltui(resReg, tmpReg, 1); // resReg = (tmpReg == 0) ? 1 : 0
      b(DONE);

      bind(IsInflated);
      // The object's monitor m is unlocked iff m->owner == NULL,
      // otherwise m->owner may contain a thread or a stack address.

      // TODO: someday avoid the ST-before-CAS penalty by
      // relocating (deferring) the following ST.
      // We should also think about trying a CAS without having
      // fetched _owner.  If the CAS is successful we may
      // avoid an RTO->RTS upgrade on the $line.
      // Without cast to int32_t a movptr will destroy r10 which is typically obj
      li(AT, (int32_t)intptr_t(markOopDesc::unused_mark()));
      st_d(AT, Address(boxReg, 0));

      ld_d(AT, Address(tmpReg, ObjectMonitor::owner_offset_in_bytes() - 2));
      // if (m->owner != 0) => AT = 0, goto slow path.
      move(scrReg, R0);
      bnez(AT, DONE_SET);

#ifndef OPT_THREAD
      get_thread(TREG) ;
#endif
      // It's inflated and appears unlocked
      addi_d(tmpReg, tmpReg, ObjectMonitor::owner_offset_in_bytes() - 2);
      cmpxchg(Address(tmpReg, 0), R0, TREG, scrReg, false, false);
      // Intentional fall-through into DONE ...

      bind(DONE_SET);
      move(resReg, scrReg);

      // DONE is a hot target - we'd really like to place it at the
      // start of cache line by padding with NOPs.
      // See the AMD and Intel software optimization manuals for the
      // most efficient "long" NOP encodings.
      // Unfortunately none of our alignment mechanisms suffice.
      bind(DONE);
      // At DONE the resReg is set as follows ...
      // Fast_Unlock uses the same protocol.
      // resReg == 1 -> Success
      // resREg == 0 -> Failure - force control through the slow-path

      // Avoid branch-to-branch on AMD processors
      // This appears to be superstition.
      if (EmitSync & 32) nop() ;

    }
}

// obj: object to unlock
// box: box address (displaced header location), killed.
// tmp: killed tmp; cannot be obj nor box.
//
// Some commentary on balanced locking:
//
// Fast_Lock and Fast_Unlock are emitted only for provably balanced lock sites.
// Methods that don't have provably balanced locking are forced to run in the
// interpreter - such methods won't be compiled to use fast_lock and fast_unlock.
// The interpreter provides two properties:
// I1:  At return-time the interpreter automatically and quietly unlocks any
//      objects acquired the current activation (frame).  Recall that the
//      interpreter maintains an on-stack list of locks currently held by
//      a frame.
// I2:  If a method attempts to unlock an object that is not held by the
//      the frame the interpreter throws IMSX.
//
// Lets say A(), which has provably balanced locking, acquires O and then calls B().
// B() doesn't have provably balanced locking so it runs in the interpreter.
// Control returns to A() and A() unlocks O.  By I1 and I2, above, we know that O
// is still locked by A().
//
// The only other source of unbalanced locking would be JNI.  The "Java Native Interface:
// Programmer's Guide and Specification" claims that an object locked by jni_monitorenter
// should not be unlocked by "normal" java-level locking and vice-versa.  The specification
// doesn't specify what will occur if a program engages in such mixed-mode locking, however.

void MacroAssembler::fast_unlock(Register objReg, Register boxReg, Register resReg,
                                 Register tmpReg, Register scrReg) {
  Label DONE, DONE_SET, Stacked, Inflated;

  guarantee(objReg != boxReg, "");
  guarantee(objReg != tmpReg, "");
  guarantee(objReg != scrReg, "");
  guarantee(boxReg != tmpReg, "");
  guarantee(boxReg != scrReg, "");

  block_comment("FastUnlock");

  if (EmitSync & 4) {
    // Disable - inhibit all inlining.  Force control through the slow-path
    move(AT, R0);
    return;
  } else
    if (EmitSync & 8) {
      Label DONE_LABEL ;
      if (UseBiasedLocking) {
        biased_locking_exit(objReg, tmpReg, DONE_LABEL);
      }
      // classic stack-locking code ...
      ld_d(tmpReg, Address(boxReg, 0)) ;
      assert_different_registers(AT, tmpReg);
      li(AT, 0x1);
      beq(tmpReg, R0, DONE_LABEL) ;

      cmpxchg(Address(objReg, 0), boxReg, tmpReg, AT, false, false);
      bind(DONE_LABEL);
    } else {
      Label CheckSucc;

      // Critically, the biased locking test must have precedence over
      // and appear before the (box->dhw == 0) recursive stack-lock test.
      if (UseBiasedLocking && !UseOptoBiasInlining) {
        Label succ, fail;
        biased_locking_exit(objReg, tmpReg, succ);
        b(fail);
        bind(succ);
        li(resReg, 1);
        b(DONE);
        bind(fail);
      }

      ld_d(tmpReg, Address(boxReg, 0)); // Examine the displaced header
      sltui(AT, tmpReg, 1);
      beqz(tmpReg, DONE_SET); // 0 indicates recursive stack-lock

      ld_d(tmpReg, Address(objReg, 0)); // Examine the object's markword
      andi(AT, tmpReg, markOopDesc::monitor_value);
      beqz(AT, Stacked); // Inflated?

      bind(Inflated);
      // It's inflated.
      // Despite our balanced locking property we still check that m->_owner == Self
      // as java routines or native JNI code called by this thread might
      // have released the lock.
      // Refer to the comments in synchronizer.cpp for how we might encode extra
      // state in _succ so we can avoid fetching EntryList|cxq.
      //
      // I'd like to add more cases in fast_lock() and fast_unlock() --
      // such as recursive enter and exit -- but we have to be wary of
      // I$ bloat, T$ effects and BP$ effects.
      //
      // If there's no contention try a 1-0 exit.  That is, exit without
      // a costly MEMBAR or CAS.  See synchronizer.cpp for details on how
      // we detect and recover from the race that the 1-0 exit admits.
      //
      // Conceptually Fast_Unlock() must execute a STST|LDST "release" barrier
      // before it STs null into _owner, releasing the lock.  Updates
      // to data protected by the critical section must be visible before
      // we drop the lock (and thus before any other thread could acquire
      // the lock and observe the fields protected by the lock).
#ifndef OPT_THREAD
      get_thread(TREG);
#endif

      // It's inflated
      ld_d(scrReg, Address(tmpReg, ObjectMonitor::owner_offset_in_bytes() - 2));
      xorr(scrReg, scrReg, TREG);

      ld_d(AT, Address(tmpReg, ObjectMonitor::recursions_offset_in_bytes() - 2));
      orr(scrReg, scrReg, AT);

      move(AT, R0);
      bnez(scrReg, DONE_SET);

      ld_d(scrReg, Address(tmpReg, ObjectMonitor::cxq_offset_in_bytes() - 2));
      ld_d(AT, Address(tmpReg, ObjectMonitor::EntryList_offset_in_bytes() - 2));
      orr(scrReg, scrReg, AT);

      move(AT, R0);
      bnez(scrReg, DONE_SET);

      membar(Assembler::Membar_mask_bits(LoadLoad|LoadStore));
      st_d(R0, Address(tmpReg, ObjectMonitor::owner_offset_in_bytes() - 2));
      li(resReg, 1);
      b(DONE);

      bind(Stacked);
      ld_d(tmpReg, Address(boxReg, 0));
      cmpxchg(Address(objReg, 0), boxReg, tmpReg, AT, false, false);

      bind(DONE_SET);
      move(resReg, AT);

      if (EmitSync & 65536) {
        bind (CheckSucc);
      }

      bind(DONE);

      // Avoid branch to branch on AMD processors
      if (EmitSync & 32768) { nop() ; }
    }
}
#endif // COMPILER2

void MacroAssembler::align(int modulus) {
  while (offset() % modulus != 0) nop();
}


void MacroAssembler::verify_FPU(int stack_depth, const char* s) {
  //Unimplemented();
}

Register caller_saved_registers[]           = {T7, T5, T6, A0, A1, A2, A3, A4, A5, A6, A7, T0, T1, T2, T3, T8, T4, S8, RA, FP};
Register caller_saved_registers_except_v0[] = {T7, T5, T6,     A1, A2, A3, A4, A5, A6, A7, T0, T1, T2, T3, T8, T4, S8, RA, FP};

  //TODO: LA
//In LA, F0~23 are all caller-saved registers
FloatRegister caller_saved_fpu_registers[] = {F0, F12, F13};

// We preserve all caller-saved register
void  MacroAssembler::pushad(){
  int i;
  // Fixed-point registers
  int len = sizeof(caller_saved_registers) / sizeof(caller_saved_registers[0]);
  addi_d(SP, SP, -1 * len * wordSize);
  for (i = 0; i < len; i++) {
    st_d(caller_saved_registers[i], SP, (len - i - 1) * wordSize);
  }

  // Floating-point registers
  len = sizeof(caller_saved_fpu_registers) / sizeof(caller_saved_fpu_registers[0]);
  addi_d(SP, SP, -1 * len * wordSize);
  for (i = 0; i < len; i++) {
    fst_d(caller_saved_fpu_registers[i], SP, (len - i - 1) * wordSize);
  }
};

void  MacroAssembler::popad(){
  int i;
  // Floating-point registers
  int len = sizeof(caller_saved_fpu_registers) / sizeof(caller_saved_fpu_registers[0]);
  for (i = 0; i < len; i++)
  {
    fld_d(caller_saved_fpu_registers[i], SP, (len - i - 1) * wordSize);
  }
  addi_d(SP, SP, len * wordSize);

  // Fixed-point registers
  len = sizeof(caller_saved_registers) / sizeof(caller_saved_registers[0]);
  for (i = 0; i < len; i++)
  {
    ld_d(caller_saved_registers[i], SP, (len - i - 1) * wordSize);
  }
  addi_d(SP, SP, len * wordSize);
};

// We preserve all caller-saved register except V0
void MacroAssembler::pushad_except_v0() {
  int i;
  // Fixed-point registers
  int len = sizeof(caller_saved_registers_except_v0) / sizeof(caller_saved_registers_except_v0[0]);
  addi_d(SP, SP, -1 * len * wordSize);
  for (i = 0; i < len; i++) {
    st_d(caller_saved_registers_except_v0[i], SP, (len - i - 1) * wordSize);
  }

  // Floating-point registers
  len = sizeof(caller_saved_fpu_registers) / sizeof(caller_saved_fpu_registers[0]);
  addi_d(SP, SP, -1 * len * wordSize);
  for (i = 0; i < len; i++) {
    fst_d(caller_saved_fpu_registers[i], SP, (len - i - 1) * wordSize);
  }
}

void MacroAssembler::popad_except_v0() {
  int i;
  // Floating-point registers
  int len = sizeof(caller_saved_fpu_registers) / sizeof(caller_saved_fpu_registers[0]);
  for (i = 0; i < len; i++) {
    fld_d(caller_saved_fpu_registers[i], SP, (len - i - 1) * wordSize);
  }
  addi_d(SP, SP, len * wordSize);

  // Fixed-point registers
  len = sizeof(caller_saved_registers_except_v0) / sizeof(caller_saved_registers_except_v0[0]);
  for (i = 0; i < len; i++) {
    ld_d(caller_saved_registers_except_v0[i], SP, (len - i - 1) * wordSize);
  }
  addi_d(SP, SP, len * wordSize);
}

void MacroAssembler::push2(Register reg1, Register reg2) {
  addi_d(SP, SP, -16);
  st_d(reg1, SP, 8);
  st_d(reg2, SP, 0);
}

void MacroAssembler::pop2(Register reg1, Register reg2) {
  ld_d(reg1, SP, 8);
  ld_d(reg2, SP, 0);
  addi_d(SP, SP, 16);
}

// for UseCompressedOops Option
void MacroAssembler::load_klass(Register dst, Register src) {
  if(UseCompressedClassPointers){
    ld_wu(dst, Address(src, oopDesc::klass_offset_in_bytes()));
    decode_klass_not_null(dst);
  } else {
    ld_d(dst, src, oopDesc::klass_offset_in_bytes());
  }
}

void MacroAssembler::store_klass(Register dst, Register src) {
  if(UseCompressedClassPointers){
    encode_klass_not_null(src);
    st_w(src, dst, oopDesc::klass_offset_in_bytes());
  } else {
    st_d(src, dst, oopDesc::klass_offset_in_bytes());
  }
}

void MacroAssembler::load_prototype_header(Register dst, Register src) {
  load_klass(dst, src);
  ld_d(dst, Address(dst, Klass::prototype_header_offset()));
}

void MacroAssembler::store_klass_gap(Register dst, Register src) {
  if (UseCompressedClassPointers) {
    st_w(src, dst, oopDesc::klass_gap_offset_in_bytes());
  }
}

void MacroAssembler::access_load_at(BasicType type, DecoratorSet decorators, Register dst, Address src,
                                    Register tmp1, Register thread_tmp) {
  BarrierSetAssembler* bs = BarrierSet::barrier_set()->barrier_set_assembler();
  decorators = AccessInternal::decorator_fixup(decorators);
  bool as_raw = (decorators & AS_RAW) != 0;
  if (as_raw) {
    bs->BarrierSetAssembler::load_at(this, decorators, type, dst, src, tmp1, thread_tmp);
  } else {
    bs->load_at(this, decorators, type, dst, src, tmp1, thread_tmp);
  }
}

void MacroAssembler::access_store_at(BasicType type, DecoratorSet decorators, Address dst, Register src,
                                     Register tmp1, Register tmp2) {
  BarrierSetAssembler* bs = BarrierSet::barrier_set()->barrier_set_assembler();
  decorators = AccessInternal::decorator_fixup(decorators);
  bool as_raw = (decorators & AS_RAW) != 0;
  if (as_raw) {
    bs->BarrierSetAssembler::store_at(this, decorators, type, dst, src, tmp1, tmp2);
  } else {
    bs->store_at(this, decorators, type, dst, src, tmp1, tmp2);
  }
}

void MacroAssembler::load_heap_oop(Register dst, Address src, Register tmp1,
                                   Register thread_tmp, DecoratorSet decorators) {
  access_load_at(T_OBJECT, IN_HEAP | decorators, dst, src, tmp1, thread_tmp);
}

// Doesn't do verfication, generates fixed size code
void MacroAssembler::load_heap_oop_not_null(Register dst, Address src, Register tmp1,
                                            Register thread_tmp, DecoratorSet decorators) {
  access_load_at(T_OBJECT, IN_HEAP | IS_NOT_NULL | decorators, dst, src, tmp1, thread_tmp);
}

void MacroAssembler::store_heap_oop(Address dst, Register src, Register tmp1,
                                    Register tmp2, DecoratorSet decorators) {
  access_store_at(T_OBJECT, IN_HEAP | decorators, dst, src, tmp1, tmp2);
}

// Used for storing NULLs.
void MacroAssembler::store_heap_oop_null(Address dst) {
  access_store_at(T_OBJECT, IN_HEAP, dst, noreg, noreg, noreg);
}

#ifdef ASSERT
void MacroAssembler::verify_heapbase(const char* msg) {
  assert (UseCompressedOops || UseCompressedClassPointers, "should be compressed");
  assert (Universe::heap() != NULL, "java heap should be initialized");
}
#endif

// Algorithm must match oop.inline.hpp encode_heap_oop.
void MacroAssembler::encode_heap_oop(Register r) {
#ifdef ASSERT
  verify_heapbase("MacroAssembler::encode_heap_oop:heap base corrupted?");
#endif
  verify_oop(r, "broken oop in encode_heap_oop");
  if (Universe::narrow_oop_base() == NULL) {
    if (Universe::narrow_oop_shift() != 0) {
      assert(LogMinObjAlignmentInBytes == Universe::narrow_oop_shift(), "decode alg wrong");
      shr(r, LogMinObjAlignmentInBytes);
    }
    return;
  }

  sub_d(AT, r, S5_heapbase);
  maskeqz(r, AT, r);
  if (Universe::narrow_oop_shift() != 0) {
    assert(LogMinObjAlignmentInBytes == Universe::narrow_oop_shift(), "decode alg wrong");
    shr(r, LogMinObjAlignmentInBytes);
  }
}

void MacroAssembler::encode_heap_oop(Register dst, Register src) {
#ifdef ASSERT
  verify_heapbase("MacroAssembler::encode_heap_oop:heap base corrupted?");
#endif
  verify_oop(src, "broken oop in encode_heap_oop");
  if (Universe::narrow_oop_base() == NULL) {
    if (Universe::narrow_oop_shift() != 0) {
      assert(LogMinObjAlignmentInBytes == Universe::narrow_oop_shift(), "decode alg wrong");
      srli_d(dst, src, LogMinObjAlignmentInBytes);
    } else {
      if (dst != src) {
        move(dst, src);
      }
    }
    return;
  }

  sub_d(AT, src, S5_heapbase);
  maskeqz(dst, AT, src);
  if (Universe::narrow_oop_shift() != 0) {
    assert(LogMinObjAlignmentInBytes == Universe::narrow_oop_shift(), "decode alg wrong");
    shr(dst, LogMinObjAlignmentInBytes);
  }
}

void MacroAssembler::encode_heap_oop_not_null(Register r) {
  assert (UseCompressedOops, "should be compressed");
#ifdef ASSERT
  if (CheckCompressedOops) {
    Label ok;
    bne(r, R0, ok);
    stop("null oop passed to encode_heap_oop_not_null");
    bind(ok);
  }
#endif
  verify_oop(r, "broken oop in encode_heap_oop_not_null");
  if (Universe::narrow_oop_base() != NULL) {
    sub_d(r, r, S5_heapbase);
  }
  if (Universe::narrow_oop_shift() != 0) {
    assert(LogMinObjAlignmentInBytes == Universe::narrow_oop_shift(), "decode alg wrong");
    shr(r, LogMinObjAlignmentInBytes);
  }
}

void MacroAssembler::encode_heap_oop_not_null(Register dst, Register src) {
  assert (UseCompressedOops, "should be compressed");
#ifdef ASSERT
  if (CheckCompressedOops) {
    Label ok;
    bne(src, R0, ok);
    stop("null oop passed to encode_heap_oop_not_null2");
    bind(ok);
  }
#endif
  verify_oop(src, "broken oop in encode_heap_oop_not_null2");
  if (Universe::narrow_oop_base() == NULL) {
    if (Universe::narrow_oop_shift() != 0) {
      assert(LogMinObjAlignmentInBytes == Universe::narrow_oop_shift(), "decode alg wrong");
      srli_d(dst, src, LogMinObjAlignmentInBytes);
    } else {
      if (dst != src) {
        move(dst, src);
      }
    }
    return;
  }

  sub_d(dst, src, S5_heapbase);
  if (Universe::narrow_oop_shift() != 0) {
    assert(LogMinObjAlignmentInBytes == Universe::narrow_oop_shift(), "decode alg wrong");
    shr(dst, LogMinObjAlignmentInBytes);
  }
}

void MacroAssembler::decode_heap_oop(Register r) {
#ifdef ASSERT
  verify_heapbase("MacroAssembler::decode_heap_oop corrupted?");
#endif
  if (Universe::narrow_oop_base() == NULL) {
    if (Universe::narrow_oop_shift() != 0) {
      assert(LogMinObjAlignmentInBytes == Universe::narrow_oop_shift(), "decode alg wrong");
      shl(r, LogMinObjAlignmentInBytes);
    }
    return;
  }

  move(AT, r);
  if (Universe::narrow_oop_shift() != 0) {
    assert(LogMinObjAlignmentInBytes == Universe::narrow_oop_shift(), "decode alg wrong");
    if (LogMinObjAlignmentInBytes <= 4) {
      alsl_d(r, r, S5_heapbase, LogMinObjAlignmentInBytes - 1);
    } else {
      shl(r, LogMinObjAlignmentInBytes);
      add_d(r, r, S5_heapbase);
    }
  } else {
    add_d(r, r, S5_heapbase);
  }
  maskeqz(r, r, AT);
  verify_oop(r, "broken oop in decode_heap_oop");
}

void MacroAssembler::decode_heap_oop(Register dst, Register src) {
#ifdef ASSERT
  verify_heapbase("MacroAssembler::decode_heap_oop corrupted?");
#endif
  if (Universe::narrow_oop_base() == NULL) {
    if (Universe::narrow_oop_shift() != 0) {
      assert(LogMinObjAlignmentInBytes == Universe::narrow_oop_shift(), "decode alg wrong");
      slli_d(dst, src, LogMinObjAlignmentInBytes);
    } else {
      if (dst != src) {
        move(dst, src);
      }
    }
    return;
  }

  Register cond;
  if (dst == src) {
    cond = AT;
    move(cond, src);
  } else {
    cond = src;
  }
  if (Universe::narrow_oop_shift() != 0) {
    assert(LogMinObjAlignmentInBytes == Universe::narrow_oop_shift(), "decode alg wrong");
    if (LogMinObjAlignmentInBytes <= 4) {
      alsl_d(dst, src, S5_heapbase, LogMinObjAlignmentInBytes - 1);
    } else {
      slli_d(dst, src, LogMinObjAlignmentInBytes);
      add_d(dst, dst, S5_heapbase);
    }
  } else {
    add_d(dst, src, S5_heapbase);
  }
  maskeqz(dst, dst, cond);
  verify_oop(dst, "broken oop in decode_heap_oop");
}

void MacroAssembler::decode_heap_oop_not_null(Register r) {
  // Note: it will change flags
  assert(UseCompressedOops, "should only be used for compressed headers");
  assert(Universe::heap() != NULL, "java heap should be initialized");
  // Cannot assert, unverified entry point counts instructions (see .ad file)
  // vtableStubs also counts instructions in pd_code_size_limit.
  // Also do not verify_oop as this is called by verify_oop.
  if (Universe::narrow_oop_shift() != 0) {
    assert(LogMinObjAlignmentInBytes == Universe::narrow_oop_shift(), "decode alg wrong");
    if (Universe::narrow_oop_base() != NULL) {
      if (LogMinObjAlignmentInBytes <= 4) {
        alsl_d(r, r, S5_heapbase, LogMinObjAlignmentInBytes - 1);
      } else {
        shl(r, LogMinObjAlignmentInBytes);
        add_d(r, r, S5_heapbase);
      }
    } else {
      shl(r, LogMinObjAlignmentInBytes);
    }
  } else {
    assert(Universe::narrow_oop_base() == NULL, "sanity");
  }
}

void MacroAssembler::decode_heap_oop_not_null(Register dst, Register src) {
  assert(UseCompressedOops, "should only be used for compressed headers");
  assert(Universe::heap() != NULL, "java heap should be initialized");
  // Cannot assert, unverified entry point counts instructions (see .ad file)
  // vtableStubs also counts instructions in pd_code_size_limit.
  // Also do not verify_oop as this is called by verify_oop.
  if (Universe::narrow_oop_shift() != 0) {
    assert(LogMinObjAlignmentInBytes == Universe::narrow_oop_shift(), "decode alg wrong");
    if (Universe::narrow_oop_base() != NULL) {
      if (LogMinObjAlignmentInBytes <= 4) {
        alsl_d(dst, src, S5_heapbase, LogMinObjAlignmentInBytes - 1);
      } else {
        slli_d(dst, src, LogMinObjAlignmentInBytes);
        add_d(dst, dst, S5_heapbase);
      }
    } else {
      slli_d(dst, src, LogMinObjAlignmentInBytes);
    }
  } else {
    assert (Universe::narrow_oop_base() == NULL, "sanity");
    if (dst != src) {
      move(dst, src);
    }
  }
}

void MacroAssembler::encode_klass_not_null(Register r) {
  if (Universe::narrow_klass_base() != NULL) {
    if (((uint64_t)Universe::narrow_klass_base() & 0xffffffff) == 0
        && Universe::narrow_klass_shift() == 0) {
      bstrpick_d(r, r, 31, 0);
      return;
    }
    assert(r != AT, "Encoding a klass in AT");
    li(AT, (int64_t)Universe::narrow_klass_base());
    sub_d(r, r, AT);
  }
  if (Universe::narrow_klass_shift() != 0) {
    assert (LogKlassAlignmentInBytes == Universe::narrow_klass_shift(), "decode alg wrong");
    shr(r, LogKlassAlignmentInBytes);
  }
}

void MacroAssembler::encode_klass_not_null(Register dst, Register src) {
  if (dst == src) {
    encode_klass_not_null(src);
  } else {
    if (Universe::narrow_klass_base() != NULL) {
      if (((uint64_t)Universe::narrow_klass_base() & 0xffffffff) == 0
          && Universe::narrow_klass_shift() == 0) {
        bstrpick_d(dst, src, 31, 0);
        return;
      }
      li(dst, (int64_t)Universe::narrow_klass_base());
      sub_d(dst, src, dst);
      if (Universe::narrow_klass_shift() != 0) {
        assert (LogKlassAlignmentInBytes == Universe::narrow_klass_shift(), "decode alg wrong");
        shr(dst, LogKlassAlignmentInBytes);
      }
    } else {
      if (Universe::narrow_klass_shift() != 0) {
        assert (LogKlassAlignmentInBytes == Universe::narrow_klass_shift(), "decode alg wrong");
        srli_d(dst, src, LogKlassAlignmentInBytes);
      } else {
        move(dst, src);
      }
    }
  }
}

void MacroAssembler::decode_klass_not_null(Register r) {
  assert(UseCompressedClassPointers, "should only be used for compressed headers");
  assert(r != AT, "Decoding a klass in AT");
  // Cannot assert, unverified entry point counts instructions (see .ad file)
  // vtableStubs also counts instructions in pd_code_size_limit.
  // Also do not verify_oop as this is called by verify_oop.
  if (Universe::narrow_klass_base() != NULL) {
    if (Universe::narrow_klass_shift() == 0) {
      if (((uint64_t)Universe::narrow_klass_base() & 0xffffffff) == 0) {
        lu32i_d(r, (uint64_t)Universe::narrow_klass_base() >> 32);
      } else {
        li(AT, (int64_t)Universe::narrow_klass_base());
        add_d(r, r, AT);
      }
    } else {
      assert(LogKlassAlignmentInBytes == Universe::narrow_klass_shift(), "decode alg wrong");
      assert(LogKlassAlignmentInBytes == Address::times_8, "klass not aligned on 64bits?");
      li(AT, (int64_t)Universe::narrow_klass_base());
      alsl_d(r, r, AT, Address::times_8 - 1);
    }
  } else {
    if (Universe::narrow_klass_shift() != 0) {
      assert(LogKlassAlignmentInBytes == Universe::narrow_klass_shift(), "decode alg wrong");
      shl(r, LogKlassAlignmentInBytes);
    }
  }
}

void MacroAssembler::decode_klass_not_null(Register dst, Register src) {
  assert(UseCompressedClassPointers, "should only be used for compressed headers");
  if (dst == src) {
    decode_klass_not_null(dst);
  } else {
    // Cannot assert, unverified entry point counts instructions (see .ad file)
    // vtableStubs also counts instructions in pd_code_size_limit.
    // Also do not verify_oop as this is called by verify_oop.
    if (Universe::narrow_klass_base() != NULL) {
      if (Universe::narrow_klass_shift() == 0) {
        if (((uint64_t)Universe::narrow_klass_base() & 0xffffffff) == 0) {
          move(dst, src);
          lu32i_d(dst, (uint64_t)Universe::narrow_klass_base() >> 32);
        } else {
          li(dst, (int64_t)Universe::narrow_klass_base());
          add_d(dst, dst, src);
        }
      } else {
        assert(LogKlassAlignmentInBytes == Universe::narrow_klass_shift(), "decode alg wrong");
        assert(LogKlassAlignmentInBytes == Address::times_8, "klass not aligned on 64bits?");
        li(dst, (int64_t)Universe::narrow_klass_base());
        alsl_d(dst, src, dst, Address::times_8 - 1);
      }
    } else {
      if (Universe::narrow_klass_shift() != 0) {
        assert(LogKlassAlignmentInBytes == Universe::narrow_klass_shift(), "decode alg wrong");
        slli_d(dst, src, LogKlassAlignmentInBytes);
      } else {
        move(dst, src);
      }
    }
  }
}

void MacroAssembler::reinit_heapbase() {
  if (UseCompressedOops || UseCompressedClassPointers) {
    if (Universe::heap() != NULL) {
      if (Universe::narrow_oop_base() == NULL) {
        move(S5_heapbase, R0);
      } else {
        li(S5_heapbase, (int64_t)Universe::narrow_ptrs_base());
      }
    } else {
      li(S5_heapbase, (intptr_t)Universe::narrow_ptrs_base_addr());
      ld_d(S5_heapbase, S5_heapbase, 0);
    }
  }
}

void MacroAssembler::check_klass_subtype(Register sub_klass,
                           Register super_klass,
                           Register temp_reg,
                           Label& L_success) {
//implement ind   gen_subtype_check
  Label L_failure;
  check_klass_subtype_fast_path(sub_klass, super_klass, temp_reg,        &L_success, &L_failure, NULL);
  check_klass_subtype_slow_path(sub_klass, super_klass, temp_reg, noreg, &L_success, NULL);
  bind(L_failure);
}

void MacroAssembler::check_klass_subtype_fast_path(Register sub_klass,
                                                   Register super_klass,
                                                   Register temp_reg,
                                                   Label* L_success,
                                                   Label* L_failure,
                                                   Label* L_slow_path,
                                        RegisterOrConstant super_check_offset) {
  assert_different_registers(sub_klass, super_klass, temp_reg);
  bool must_load_sco = (super_check_offset.constant_or_zero() == -1);
  if (super_check_offset.is_register()) {
    assert_different_registers(sub_klass, super_klass,
                               super_check_offset.as_register());
  } else if (must_load_sco) {
    assert(temp_reg != noreg, "supply either a temp or a register offset");
  }

  Label L_fallthrough;
  int label_nulls = 0;
  if (L_success == NULL)   { L_success   = &L_fallthrough; label_nulls++; }
  if (L_failure == NULL)   { L_failure   = &L_fallthrough; label_nulls++; }
  if (L_slow_path == NULL) { L_slow_path = &L_fallthrough; label_nulls++; }
  assert(label_nulls <= 1, "at most one NULL in the batch");

  int sc_offset = in_bytes(Klass::secondary_super_cache_offset());
  int sco_offset = in_bytes(Klass::super_check_offset_offset());
  // If the pointers are equal, we are done (e.g., String[] elements).
  // This self-check enables sharing of secondary supertype arrays among
  // non-primary types such as array-of-interface.  Otherwise, each such
  // type would need its own customized SSA.
  // We move this check to the front of the fast path because many
  // type checks are in fact trivially successful in this manner,
  // so we get a nicely predicted branch right at the start of the check.
  beq(sub_klass, super_klass, *L_success);
  // Check the supertype display:
  if (must_load_sco) {
    ld_wu(temp_reg, super_klass, sco_offset);
    super_check_offset = RegisterOrConstant(temp_reg);
  }
  slli_d(AT, super_check_offset.register_or_noreg(), Address::times_1);
  add_d(AT, sub_klass, AT);
  ld_d(AT, AT, super_check_offset.constant_or_zero()*Address::times_1);

  // This check has worked decisively for primary supers.
  // Secondary supers are sought in the super_cache ('super_cache_addr').
  // (Secondary supers are interfaces and very deeply nested subtypes.)
  // This works in the same check above because of a tricky aliasing
  // between the super_cache and the primary super display elements.
  // (The 'super_check_addr' can address either, as the case requires.)
  // Note that the cache is updated below if it does not help us find
  // what we need immediately.
  // So if it was a primary super, we can just fail immediately.
  // Otherwise, it's the slow path for us (no success at this point).

  if (super_check_offset.is_register()) {
    beq(super_klass, AT, *L_success);
    addi_d(AT, super_check_offset.as_register(), -sc_offset);
    if (L_failure == &L_fallthrough) {
      beq(AT, R0, *L_slow_path);
    } else {
      bne_far(AT, R0, *L_failure);
      b(*L_slow_path);
    }
  } else if (super_check_offset.as_constant() == sc_offset) {
    // Need a slow path; fast failure is impossible.
    if (L_slow_path == &L_fallthrough) {
      beq(super_klass, AT, *L_success);
    } else {
      bne(super_klass, AT, *L_slow_path);
      b(*L_success);
    }
  } else {
    // No slow path; it's a fast decision.
    if (L_failure == &L_fallthrough) {
      beq(super_klass, AT, *L_success);
    } else {
      bne_far(super_klass, AT, *L_failure);
      b(*L_success);
    }
  }

  bind(L_fallthrough);
}

void MacroAssembler::check_klass_subtype_slow_path(Register sub_klass,
                                                   Register super_klass,
                                                   Register temp_reg,
                                                   Register temp2_reg,
                                                   Label* L_success,
                                                   Label* L_failure,
                                                   bool set_cond_codes) {
  if (temp2_reg == noreg)
    temp2_reg = TSR;
  assert_different_registers(sub_klass, super_klass, temp_reg, temp2_reg);
#define IS_A_TEMP(reg) ((reg) == temp_reg || (reg) == temp2_reg)

  Label L_fallthrough;
  int label_nulls = 0;
  if (L_success == NULL)   { L_success   = &L_fallthrough; label_nulls++; }
  if (L_failure == NULL)   { L_failure   = &L_fallthrough; label_nulls++; }
  assert(label_nulls <= 1, "at most one NULL in the batch");

  // a couple of useful fields in sub_klass:
  int ss_offset = in_bytes(Klass::secondary_supers_offset());
  int sc_offset = in_bytes(Klass::secondary_super_cache_offset());
  Address secondary_supers_addr(sub_klass, ss_offset);
  Address super_cache_addr(     sub_klass, sc_offset);

  // Do a linear scan of the secondary super-klass chain.
  // This code is rarely used, so simplicity is a virtue here.
  // The repne_scan instruction uses fixed registers, which we must spill.
  // Don't worry too much about pre-existing connections with the input regs.

#ifndef PRODUCT
  int* pst_counter = &SharedRuntime::_partial_subtype_ctr;
  ExternalAddress pst_counter_addr((address) pst_counter);
#endif //PRODUCT

  // We will consult the secondary-super array.
  ld_d(temp_reg, secondary_supers_addr);
  // Load the array length.
  ld_w(temp2_reg, Address(temp_reg, Array<Klass*>::length_offset_in_bytes()));
  // Skip to start of data.
  addi_d(temp_reg, temp_reg, Array<Klass*>::base_offset_in_bytes());

  Label Loop, subtype;
  bind(Loop);
  beq(temp2_reg, R0, *L_failure);
  ld_d(AT, temp_reg, 0);
  addi_d(temp_reg, temp_reg, 1 * wordSize);
  beq(AT, super_klass, subtype);
  addi_d(temp2_reg, temp2_reg, -1);
  b(Loop);

  bind(subtype);
  st_d(super_klass, super_cache_addr);
  if (L_success != &L_fallthrough) {
    b(*L_success);
  }

  // Success.  Cache the super we found and proceed in triumph.
#undef IS_A_TEMP

  bind(L_fallthrough);
}

void MacroAssembler::get_vm_result(Register oop_result, Register java_thread) {
  ld_d(oop_result, Address(java_thread, JavaThread::vm_result_offset()));
  st_d(R0, Address(java_thread, JavaThread::vm_result_offset()));
  verify_oop(oop_result, "broken oop in call_VM_base");
}

void MacroAssembler::get_vm_result_2(Register metadata_result, Register java_thread) {
  ld_d(metadata_result, Address(java_thread, JavaThread::vm_result_2_offset()));
  st_d(R0, Address(java_thread, JavaThread::vm_result_2_offset()));
}

Address MacroAssembler::argument_address(RegisterOrConstant arg_slot,
                                         int extra_slot_offset) {
  // cf. TemplateTable::prepare_invoke(), if (load_receiver).
  int stackElementSize = Interpreter::stackElementSize;
  int offset = Interpreter::expr_offset_in_bytes(extra_slot_offset+0);
#ifdef ASSERT
  int offset1 = Interpreter::expr_offset_in_bytes(extra_slot_offset+1);
  assert(offset1 - offset == stackElementSize, "correct arithmetic");
#endif
  Register             scale_reg    = NOREG;
  Address::ScaleFactor scale_factor = Address::no_scale;
  if (arg_slot.is_constant()) {
    offset += arg_slot.as_constant() * stackElementSize;
  } else {
    scale_reg    = arg_slot.as_register();
    scale_factor = Address::times_8;
  }
  // We don't push RA on stack in prepare_invoke.
  //  offset += wordSize;           // return PC is on stack
  if(scale_reg==NOREG) return Address(SP, offset);
  else {
  alsl_d(scale_reg, scale_reg, SP, scale_factor - 1);
  return Address(scale_reg, offset);
  }
}

SkipIfEqual::~SkipIfEqual() {
  _masm->bind(_label);
}

void MacroAssembler::load_sized_value(Register dst, Address src, size_t size_in_bytes, bool is_signed, Register dst2) {
  switch (size_in_bytes) {
  case  8:  ld_d(dst, src); break;
  case  4:  ld_w(dst, src); break;
  case  2:  is_signed ? ld_h(dst, src) : ld_hu(dst, src); break;
  case  1:  is_signed ? ld_b( dst, src) : ld_bu( dst, src); break;
  default:  ShouldNotReachHere();
  }
}

void MacroAssembler::store_sized_value(Address dst, Register src, size_t size_in_bytes, Register src2) {
  switch (size_in_bytes) {
  case  8:  st_d(src, dst); break;
  case  4:  st_w(src, dst); break;
  case  2:  st_h(src, dst); break;
  case  1:  st_b(src, dst); break;
  default:  ShouldNotReachHere();
  }
}

// Look up the method for a megamorphic invokeinterface call.
// The target method is determined by <intf_klass, itable_index>.
// The receiver klass is in recv_klass.
// On success, the result will be in method_result, and execution falls through.
// On failure, execution transfers to the given label.
void MacroAssembler::lookup_interface_method(Register recv_klass,
                                             Register intf_klass,
                                             RegisterOrConstant itable_index,
                                             Register method_result,
                                             Register scan_temp,
                                             Label& L_no_such_interface,
                                             bool return_method) {
  assert_different_registers(recv_klass, intf_klass, scan_temp, AT);
  assert_different_registers(method_result, intf_klass, scan_temp, AT);
  assert(recv_klass != method_result || !return_method,
         "recv_klass can be destroyed when method isn't needed");

  assert(itable_index.is_constant() || itable_index.as_register() == method_result,
         "caller must use same register for non-constant itable index as for method");

  // Compute start of first itableOffsetEntry (which is at the end of the vtable)
  int vtable_base = in_bytes(Klass::vtable_start_offset());
  int itentry_off = itableMethodEntry::method_offset_in_bytes();
  int scan_step   = itableOffsetEntry::size() * wordSize;
  int vte_size    = vtableEntry::size() * wordSize;
  Address::ScaleFactor times_vte_scale = Address::times_ptr;
  assert(vte_size == wordSize, "else adjust times_vte_scale");

  ld_w(scan_temp, Address(recv_klass, Klass::vtable_length_offset()));

  // %%% Could store the aligned, prescaled offset in the klassoop.
  alsl_d(scan_temp, scan_temp, recv_klass, times_vte_scale - 1);
  addi_d(scan_temp, scan_temp, vtable_base);

  if (return_method) {
    // Adjust recv_klass by scaled itable_index, so we can free itable_index.
    if (itable_index.is_constant()) {
      li(AT, (itable_index.as_constant() * itableMethodEntry::size() * wordSize) + itentry_off);
      add_d(recv_klass, recv_klass, AT);
    } else {
      assert(itableMethodEntry::size() * wordSize == wordSize, "adjust the scaling in the code below");
      alsl_d(AT, itable_index.as_register(), recv_klass, (int)Address::times_ptr - 1);
      addi_d(recv_klass, AT, itentry_off);
    }
  }

  Label search, found_method;

  ld_d(method_result, Address(scan_temp, itableOffsetEntry::interface_offset_in_bytes()));
  beq(intf_klass, method_result, found_method);

  bind(search);
  // Check that the previous entry is non-null.  A null entry means that
  // the receiver class doesn't implement the interface, and wasn't the
  // same as when the caller was compiled.
  beqz(method_result, L_no_such_interface);
  addi_d(scan_temp, scan_temp, scan_step);
  ld_d(method_result, Address(scan_temp, itableOffsetEntry::interface_offset_in_bytes()));
  bne(intf_klass, method_result, search);

  bind(found_method);
  if (return_method) {
    // Got a hit.
    ld_wu(scan_temp, Address(scan_temp, itableOffsetEntry::offset_offset_in_bytes()));
    ldx_d(method_result, recv_klass, scan_temp);
  }
}

// virtual method calling
void MacroAssembler::lookup_virtual_method(Register recv_klass,
                                           RegisterOrConstant vtable_index,
                                           Register method_result) {
  const int base = in_bytes(Klass::vtable_start_offset());
  assert(vtableEntry::size() * wordSize == wordSize, "else adjust the scaling in the code below");

  if (vtable_index.is_constant()) {
    li(AT, vtable_index.as_constant());
    alsl_d(AT, AT, recv_klass, Address::times_ptr - 1);
  } else {
    alsl_d(AT, vtable_index.as_register(), recv_klass, Address::times_ptr - 1);
  }

  ld_d(method_result, AT, base + vtableEntry::method_offset_in_bytes());
}

#ifdef COMPILER2
// Compare strings, used for char[] and byte[].
void MacroAssembler::string_compare(Register str1, Register str2,
                                    Register cnt1, Register cnt2, Register result,
                                    int ae) {
  Label L, Loop, haveResult, done;

  bool isLL = ae == StrIntrinsicNode::LL;
  bool isLU = ae == StrIntrinsicNode::LU;
  bool isUL = ae == StrIntrinsicNode::UL;

  bool str1_isL = isLL || isLU;
  bool str2_isL = isLL || isUL;

  if (!str1_isL) srli_w(cnt1, cnt1, 1);
  if (!str2_isL) srli_w(cnt2, cnt2, 1);

  // compute the and difference of lengths (in result)
  sub_d(result, cnt1, cnt2); // result holds the difference of two lengths

  // compute the shorter length (in cnt1)
  bge(cnt2, cnt1, Loop);
  move(cnt1, cnt2);

  // Now the shorter length is in cnt1 and cnt2 can be used as a tmp register
  bind(Loop);                        // Loop begin
  if (str1_isL) {
    ld_bu(AT, str1, 0);
  } else {
    ld_hu(AT, str1, 0);
  }
  beq(cnt1, R0, done);

  // compare current character
  if (str2_isL) {
    ld_bu(cnt2, str2, 0);
  } else {
    ld_hu(cnt2, str2, 0);
  }
  addi_d(str1, str1, str1_isL ? 1 : 2);
  bne(AT, cnt2, haveResult);
  addi_d(str2, str2, str2_isL ? 1 : 2);
  addi_d(cnt1, cnt1, -1);
  b(Loop);

  bind(haveResult);
  sub_d(result, AT, cnt2);

  bind(done);
}

// Compare char[] or byte[] arrays or substrings.
void MacroAssembler::arrays_equals(Register str1, Register str2,
                                   Register cnt, Register tmp1, Register tmp2, Register result,
                                   bool is_char) {
  Label Loop, LoopEnd, True, False;

  addi_d(result, R0, 1);
  beq(str1, str2, True);  // same char[] ?
  beqz(cnt, True);

  addi_d(AT, R0, is_char ? wordSize/2 : wordSize);
  bind(Loop);
  blt(cnt, AT, LoopEnd);
  ld_d(tmp1, str1, 0);
  ld_d(tmp2, str2, 0);
  bne(tmp1, tmp2, False);
  addi_d(str1, str1, 8);
  addi_d(str2, str2, 8);
  addi_d(cnt, cnt, is_char ? -wordSize/2 : -wordSize);
  b(Loop);

  bind(LoopEnd);
  beqz(cnt, True);
  // compare current character
  if (is_char) {
    ld_hu(tmp1, str1, 0);
    ld_hu(tmp2, str2, 0);
  } else {
    ld_bu(tmp1, str1, 0);
    ld_bu(tmp2, str2, 0);
  }
  bne(tmp1, tmp2, False);
  addi_d(str1, str1, is_char ? 2 : 1);
  addi_d(str2, str2, is_char ? 2 : 1);
  addi_d(cnt, cnt, -1);
  b(LoopEnd);

  bind(False);
  addi_d(result, R0, 0);

  bind(True);
}
#endif // COMPILER2

void MacroAssembler::load_byte_map_base(Register reg) {
  jbyte *byte_map_base =
    ((CardTableBarrierSet*)(BarrierSet::barrier_set()))->card_table()->byte_map_base();

  // Strictly speaking the byte_map_base isn't an address at all, and it might
  // even be negative. It is thus materialised as a constant.
  li(reg, (uint64_t)byte_map_base);
}

// This method checks if provided byte array contains byte with highest bit set.
void MacroAssembler::has_negatives(Register ary1, Register len, Register result) {
    Label Loop, End, Nega, Done;

    orr(result, R0, R0);
    bge(R0, len, Done);

    li(AT, 0x8080808080808080);

    addi_d(len, len, -8);
    blt(len, R0, End);

  bind(Loop);
    ld_d(result, ary1, 0);
    andr(result, result, AT);
    bnez(result, Nega);
    beqz(len, Done);
    addi_d(len, len, -8);
    addi_d(ary1, ary1, 8);
    bge(len, R0, Loop);

  bind(End);
    ld_d(result, ary1, 0);
    slli_d(len, len, 3);
    sub_d(len, R0, len);
    sll_d(result, result, len);
    andr(result, result, AT);
    beqz(result, Done);

  bind(Nega);
    ori(result, R0, 1);

  bind(Done);
}

// Compress char[] to byte[]. len must be positive int.
// jtreg: TestStringIntrinsicRangeChecks.java
void MacroAssembler::char_array_compress(Register src, Register dst,
                                         Register len, Register result,
                                         Register tmp1, Register tmp2,
                                         Register tmp3) {
  Label Loop, Done, Once, Fail;

  move(result, len);
  bge(R0, result, Done);

  srli_w(AT, len, 2);
  andi(len, len, 3);

  li(tmp3, 0xff00ff00ff00ff00);

  bind(Loop);
    beqz(AT, Once);
    ld_d(tmp1, src, 0);
    andr(tmp2, tmp3, tmp1);          // not latin-1, stop here
    bnez(tmp2, Fail);

    // 0x00a100b200c300d4 -> 0x00000000a1b2c3d4
    srli_d(tmp2, tmp1, 8);
    orr(tmp2, tmp2, tmp1);           // 0x00a1a1b2b2c3c3d4
    bstrpick_d(tmp1, tmp2, 47, 32);  // 0x0000a1b2
    slli_d(tmp1, tmp1, 16);          // 0xa1b20000
    bstrins_d(tmp1, tmp2, 15, 0);    // 0xa1b2c3d4

    st_w(tmp1, dst, 0);
    addi_w(AT, AT, -1);
    addi_d(dst, dst, 4);
    addi_d(src, src, 8);
    b(Loop);

  bind(Once);
    beqz(len, Done);
    ld_d(AT, src, 0);

    bstrpick_d(tmp1, AT, 15, 0);
    andr(tmp2, tmp3, tmp1);
    bnez(tmp2, Fail);
    st_b(tmp1, dst, 0);
    addi_w(len, len, -1);

    beqz(len, Done);
    bstrpick_d(tmp1, AT, 31, 16);
    andr(tmp2, tmp3, tmp1);
    bnez(tmp2, Fail);
    st_b(tmp1, dst, 1);
    addi_w(len, len, -1);

    beqz(len, Done);
    bstrpick_d(tmp1, AT, 47, 32);
    andr(tmp2, tmp3, tmp1);
    bnez(tmp2, Fail);
    st_b(tmp1, dst, 2);
    b(Done);

  bind(Fail);
    move(result, R0);

  bind(Done);
}

// Inflate byte[] to char[]. len must be positive int.
// jtreg:test/jdk/sun/nio/cs/FindDecoderBugs.java
void MacroAssembler::byte_array_inflate(Register src, Register dst, Register len,
                                        Register tmp1, Register tmp2) {
  Label Loop, Once, Done;

  bge(R0, len, Done);

  srli_w(AT, len, 2);
  andi(len, len, 3);

  bind(Loop);
    beqz(AT, Once);
    ld_wu(tmp1, src, 0);

    // 0x00000000a1b2c3d4 -> 0x00a100b200c300d4
    bstrpick_d(tmp2, tmp1, 7, 0);
    srli_d(tmp1, tmp1, 8);
    bstrins_d(tmp2, tmp1, 23, 16);
    srli_d(tmp1, tmp1, 8);
    bstrins_d(tmp2, tmp1, 39, 32);
    srli_d(tmp1, tmp1, 8);
    bstrins_d(tmp2, tmp1, 55, 48);

    st_d(tmp2, dst, 0);
    addi_w(AT, AT, -1);
    addi_d(dst, dst, 8);
    addi_d(src, src, 4);
    b(Loop);

  bind(Once);
    beqz(len, Done);
    ld_wu(tmp1, src, 0);

    bstrpick_d(tmp2, tmp1, 7, 0);
    st_h(tmp2, dst, 0);
    addi_w(len, len, -1);

    beqz(len, Done);
    bstrpick_d(tmp2, tmp1, 15, 8);
    st_h(tmp2, dst, 2);
    addi_w(len, len, -1);

    beqz(len, Done);
    bstrpick_d(tmp2, tmp1, 23, 16);
    st_h(tmp2, dst, 4);

  bind(Done);
}

void MacroAssembler::string_indexof_char(Register str1, Register cnt1,
                                            Register ch, Register result,
                                            Register tmp1, Register tmp2,
                                            Register tmp3)
{
  Label CH1_LOOP, HAS_ZERO, DO1_SHORT, DO1_LOOP, NOMATCH, DONE;

  beqz(cnt1, NOMATCH);

  move(result, R0);
  ori(tmp1, R0, 4);
  blt(cnt1, tmp1, DO1_LOOP);

  // UTF-16 char occupies 16 bits
  // ch -> chchchch
  bstrins_d(ch, ch, 31, 16);
  bstrins_d(ch, ch, 63, 32);

  li(tmp2, 0x0001000100010001);
  li(tmp3, 0x7fff7fff7fff7fff);

  bind(CH1_LOOP);
    ld_d(AT, str1, 0);
    xorr(AT, ch, AT);
    sub_d(tmp1, AT, tmp2);
    orr(AT, AT, tmp3);
    andn(tmp1, tmp1, AT);
    bnez(tmp1, HAS_ZERO);
    addi_d(str1, str1, 8);
    addi_d(result, result, 4);

    // meet the end of string
    beq(cnt1, result, NOMATCH);

    addi_d(tmp1, result, 4);
    bge(tmp1, cnt1, DO1_SHORT);
    b(CH1_LOOP);

  bind(HAS_ZERO);
    ctz_d(tmp1, tmp1);
    srli_d(tmp1, tmp1, 4);
    add_d(result, result, tmp1);
    b(DONE);

  // restore ch
  bind(DO1_SHORT);
    bstrpick_d(ch, ch, 15, 0);

  bind(DO1_LOOP);
    ld_hu(tmp1, str1, 0);
    beq(ch, tmp1, DONE);
    addi_d(str1, str1, 2);
    addi_d(result, result, 1);
    blt(result, cnt1, DO1_LOOP);

  bind(NOMATCH);
    addi_d(result, R0, -1);

  bind(DONE);
}

void MacroAssembler::clear_jweak_tag(Register possibly_jweak) {
  const int32_t inverted_jweak_mask = ~static_cast<int32_t>(JNIHandles::weak_tag_mask);
  STATIC_ASSERT(inverted_jweak_mask == -2); // otherwise check this code
  // The inverted mask is sign-extended
  li(AT, inverted_jweak_mask);
  andr(possibly_jweak, AT, possibly_jweak);
}

void MacroAssembler::resolve_jobject(Register value,
                                     Register thread,
                                     Register tmp) {
  assert_different_registers(value, thread, tmp);
  Label done, not_weak;
  beq(value, R0, done);                // Use NULL as-is.
  li(AT, JNIHandles::weak_tag_mask); // Test for jweak tag.
  andr(AT, value, AT);
  beq(AT, R0, not_weak);
  // Resolve jweak.
  access_load_at(T_OBJECT, IN_NATIVE | ON_PHANTOM_OOP_REF,
                 value, Address(value, -JNIHandles::weak_tag_value), tmp, thread);
  verify_oop(value);
  b(done);
  bind(not_weak);
  // Resolve (untagged) jobject.
  access_load_at(T_OBJECT, IN_NATIVE, value, Address(value, 0), tmp, thread);
  verify_oop(value);
  bind(done);
}

void MacroAssembler::lea(Register rd, Address src) {
  Register dst   = rd;
  Register base  = src.base();
  Register index = src.index();

  int scale = src.scale();
  int disp  = src.disp();

  if (index == noreg) {
    if (is_simm(disp, 12)) {
      addi_d(dst, base, disp);
    } else {
      lu12i_w(AT, split_low20(disp >> 12));
      if (split_low12(disp))
        ori(AT, AT, split_low12(disp));
      add_d(dst, base, AT);
    }
  } else {
    if (scale == 0) {
      if (is_simm(disp, 12)) {
        add_d(AT, base, index);
        addi_d(dst, AT, disp);
      } else {
        lu12i_w(AT, split_low20(disp >> 12));
        if (split_low12(disp))
          ori(AT, AT, split_low12(disp));
        add_d(AT, base, AT);
        add_d(dst, AT, index);
      }
    } else {
      if (is_simm(disp, 12)) {
        alsl_d(AT, index, base, scale - 1);
        addi_d(dst, AT, disp);
      } else {
        lu12i_w(AT, split_low20(disp >> 12));
        if (split_low12(disp))
          ori(AT, AT, split_low12(disp));
        add_d(AT, AT, base);
        alsl_d(dst, index, AT, scale - 1);
      }
    }
  }
}

void MacroAssembler::lea(Register dst, AddressLiteral adr) {
  code_section()->relocate(pc(), adr.rspec());
  pcaddi(dst, (adr.target() - pc()) >> 2);
}

int MacroAssembler::patched_branch(int dest_pos, int inst, int inst_pos) {
  int v = (dest_pos - inst_pos) >> 2;
  switch(high(inst, 6)) {
  case beq_op:
  case bne_op:
  case blt_op:
  case bge_op:
  case bltu_op:
  case bgeu_op:
    assert(is_simm16(v), "must be simm16");
#ifndef PRODUCT
    if(!is_simm16(v))
    {
      tty->print_cr("must be simm16");
      tty->print_cr("Inst: %x", inst);
    }
#endif

    inst &= 0xfc0003ff;
    inst |= ((v & 0xffff) << 10);
    break;
  case beqz_op:
  case bnez_op:
  case bccondz_op:
    assert(is_simm(v, 21), "must be simm21");
#ifndef PRODUCT
    if(!is_simm(v, 21))
    {
      tty->print_cr("must be simm21");
      tty->print_cr("Inst: %x", inst);
    }
#endif

    inst &= 0xfc0003e0;
    inst |= ( ((v & 0xffff) << 10) | ((v >> 16) & 0x1f) );
    break;
  case b_op:
  case bl_op:
    assert(is_simm(v, 26), "must be simm26");
#ifndef PRODUCT
    if(!is_simm(v, 26))
    {
      tty->print_cr("must be simm26");
      tty->print_cr("Inst: %x", inst);
    }
#endif

    inst &= 0xfc000000;
    inst |= ( ((v & 0xffff) << 10) | ((v >> 16) & 0x3ff) );
    break;
  default:
    ShouldNotReachHere();
    break;
  }
  return inst;
}

void MacroAssembler::cmp_cmov(Register  op1,
                              Register  op2,
                              Register  dst,
                              Register  src1,
                              Register  src2,
                              CMCompare cmp,
                              bool      is_signed) {
  switch (cmp) {
    case EQ:
      sub_d(AT, op1, op2);
      if (dst == src2) {
        masknez(dst, src2, AT);
        maskeqz(AT, src1, AT);
      } else {
        maskeqz(dst, src1, AT);
        masknez(AT, src2, AT);
      }
      break;

    case NE:
      sub_d(AT, op1, op2);
      if (dst == src2) {
        maskeqz(dst, src2, AT);
        masknez(AT, src1, AT);
      } else {
        masknez(dst, src1, AT);
        maskeqz(AT, src2, AT);
      }
      break;

    case GT:
      if (is_signed) {
        slt(AT, op2, op1);
      } else {
        sltu(AT, op2, op1);
      }
      if(dst == src2) {
        maskeqz(dst, src2, AT);
        masknez(AT, src1, AT);
      } else {
        masknez(dst, src1, AT);
        maskeqz(AT, src2, AT);
      }
      break;
    case GE:
      if (is_signed) {
        slt(AT, op1, op2);
      } else {
        sltu(AT, op1, op2);
      }
      if(dst == src2) {
        masknez(dst, src2, AT);
        maskeqz(AT, src1, AT);
      } else {
        maskeqz(dst, src1, AT);
        masknez(AT, src2, AT);
      }
      break;

    case LT:
      if (is_signed) {
        slt(AT, op1, op2);
      } else {
        sltu(AT, op1, op2);
      }
      if(dst == src2) {
        maskeqz(dst, src2, AT);
        masknez(AT, src1, AT);
      } else {
        masknez(dst, src1, AT);
        maskeqz(AT, src2, AT);
      }
      break;
    case LE:
      if (is_signed) {
        slt(AT, op2, op1);
      } else {
        sltu(AT, op2, op1);
      }
      if(dst == src2) {
        masknez(dst, src2, AT);
        maskeqz(AT, src1, AT);
      } else {
        maskeqz(dst, src1, AT);
        masknez(AT, src2, AT);
      }
      break;
    default:
      Unimplemented();
  }
  OR(dst, dst, AT);
}

void MacroAssembler::cmp_cmov(Register  op1,
                              Register  op2,
                              Register  dst,
                              Register  src,
                              CMCompare cmp,
                              bool      is_signed) {
  switch (cmp) {
    case EQ:
      sub_d(AT, op1, op2);
      maskeqz(dst, dst, AT);
      masknez(AT, src, AT);
      break;

    case NE:
      sub_d(AT, op1, op2);
      masknez(dst, dst, AT);
      maskeqz(AT, src, AT);
      break;

    case GT:
      if (is_signed) {
        slt(AT, op2, op1);
      } else {
        sltu(AT, op2, op1);
      }
      masknez(dst, dst, AT);
      maskeqz(AT, src, AT);
      break;

    case GE:
      if (is_signed) {
        slt(AT, op1, op2);
      } else {
        sltu(AT, op1, op2);
      }
      maskeqz(dst, dst, AT);
      masknez(AT, src, AT);
      break;

    case LT:
      if (is_signed) {
        slt(AT, op1, op2);
      } else {
        sltu(AT, op1, op2);
      }
      masknez(dst, dst, AT);
      maskeqz(AT, src, AT);
      break;

    case LE:
      if (is_signed) {
        slt(AT, op2, op1);
      } else {
        sltu(AT, op2, op1);
      }
      maskeqz(dst, dst, AT);
      masknez(AT, src, AT);
      break;

    default:
      Unimplemented();
  }
  OR(dst, dst, AT);
}


void MacroAssembler::cmp_cmov(FloatRegister op1,
                              FloatRegister op2,
                              Register      dst,
                              Register      src,
                              FloatRegister tmp1,
                              FloatRegister tmp2,
                              CMCompare     cmp,
                              bool          is_float) {
  movgr2fr_d(tmp1, dst);
  movgr2fr_d(tmp2, src);

  switch(cmp) {
    case EQ:
      if (is_float) {
        fcmp_ceq_s(FCC0, op1, op2);
      } else {
        fcmp_ceq_d(FCC0, op1, op2);
      }
      fsel(tmp1, tmp1, tmp2, FCC0);
      break;

    case NE:
      if (is_float) {
        fcmp_ceq_s(FCC0, op1, op2);
      } else {
        fcmp_ceq_d(FCC0, op1, op2);
      }
      fsel(tmp1, tmp2, tmp1, FCC0);
      break;

    case GT:
      if (is_float) {
        fcmp_cule_s(FCC0, op1, op2);
      } else {
        fcmp_cule_d(FCC0, op1, op2);
      }
      fsel(tmp1, tmp2, tmp1, FCC0);
      break;

    case GE:
      if (is_float) {
        fcmp_cult_s(FCC0, op1, op2);
      } else {
        fcmp_cult_d(FCC0, op1, op2);
      }
      fsel(tmp1, tmp2, tmp1, FCC0);
      break;

    case LT:
      if (is_float) {
        fcmp_cult_s(FCC0, op1, op2);
      } else {
        fcmp_cult_d(FCC0, op1, op2);
      }
      fsel(tmp1, tmp1, tmp2, FCC0);
      break;

    case LE:
      if (is_float) {
        fcmp_cule_s(FCC0, op1, op2);
      } else {
        fcmp_cule_d(FCC0, op1, op2);
      }
      fsel(tmp1, tmp1, tmp2, FCC0);
      break;

    default:
      Unimplemented();
  }

  movfr2gr_d(dst, tmp1);
}

void MacroAssembler::cmp_cmov(FloatRegister op1,
                              FloatRegister op2,
                              FloatRegister dst,
                              FloatRegister src,
                              CMCompare     cmp,
                              bool          is_float) {
  switch(cmp) {
    case EQ:
      if (!is_float) {
        fcmp_ceq_d(FCC0, op1, op2);
      } else {
        fcmp_ceq_s(FCC0, op1, op2);
      }
      fsel(dst, dst, src, FCC0);
      break;

    case NE:
      if (!is_float) {
        fcmp_ceq_d(FCC0, op1, op2);
      } else {
        fcmp_ceq_s(FCC0, op1, op2);
      }
      fsel(dst, src, dst, FCC0);
      break;

    case GT:
      if (!is_float) {
        fcmp_cule_d(FCC0, op1, op2);
      } else {
        fcmp_cule_s(FCC0, op1, op2);
      }
      fsel(dst, src, dst, FCC0);
      break;

    case GE:
      if (!is_float) {
        fcmp_cult_d(FCC0, op1, op2);
      } else {
        fcmp_cult_s(FCC0, op1, op2);
      }
      fsel(dst, src, dst, FCC0);
      break;

    case LT:
      if (!is_float) {
        fcmp_cult_d(FCC0, op1, op2);
      } else {
        fcmp_cult_s(FCC0, op1, op2);
      }
      fsel(dst, dst, src, FCC0);
      break;

    case LE:
      if (!is_float) {
        fcmp_cule_d(FCC0, op1, op2);
      } else {
        fcmp_cule_s(FCC0, op1, op2);
      }
      fsel(dst, dst, src, FCC0);
      break;

    default:
      Unimplemented();
  }
}

void MacroAssembler::cmp_cmov(Register      op1,
                              Register      op2,
                              FloatRegister dst,
                              FloatRegister src,
                              FloatRegister tmp1,
                              FloatRegister tmp2,
                              CMCompare     cmp) {
  movgr2fr_w(tmp1, R0);

  switch (cmp) {
    case EQ:
      sub_d(AT, op1, op2);
      movgr2fr_w(tmp2, AT);
      fcmp_ceq_s(FCC0, tmp1, tmp2);
      fsel(dst, dst, src, FCC0);
      break;

    case NE:
      sub_d(AT, op1, op2);
      movgr2fr_w(tmp2, AT);
      fcmp_ceq_s(FCC0, tmp1, tmp2);
      fsel(dst, src, dst, FCC0);
      break;

    case GT:
      slt(AT, op2, op1);
      movgr2fr_w(tmp2, AT);
      fcmp_ceq_s(FCC0, tmp1, tmp2);
      fsel(dst, src, dst, FCC0);
      break;

    case GE:
      slt(AT, op1, op2);
      movgr2fr_w(tmp2, AT);
      fcmp_ceq_s(FCC0, tmp1, tmp2);
      fsel(dst, dst, src, FCC0);
      break;

    case LT:
      slt(AT, op1, op2);
      movgr2fr_w(tmp2, AT);
      fcmp_ceq_s(FCC0, tmp1, tmp2);
      fsel(dst, src, dst, FCC0);
      break;

    case LE:
      slt(AT, op2, op1);
      movgr2fr_w(tmp2, AT);
      fcmp_ceq_s(FCC0, tmp1, tmp2);
      fsel(dst, dst, src, FCC0);
      break;

    default:
      Unimplemented();
  }
}

void MacroAssembler::loadstore(Register reg, Register base, int disp, int type) {
  switch (type) {
    case STORE_BYTE:   st_b (reg, base, disp); break;
    case STORE_CHAR:
    case STORE_SHORT:  st_h (reg, base, disp); break;
    case STORE_INT:    st_w (reg, base, disp); break;
    case STORE_LONG:   st_d (reg, base, disp); break;
    case LOAD_BYTE:    ld_b (reg, base, disp); break;
    case LOAD_U_BYTE:  ld_bu(reg, base, disp); break;
    case LOAD_SHORT:   ld_h (reg, base, disp); break;
    case LOAD_U_SHORT: ld_hu(reg, base, disp); break;
    case LOAD_INT:     ld_w (reg, base, disp); break;
    case LOAD_U_INT:   ld_wu(reg, base, disp); break;
    case LOAD_LONG:    ld_d (reg, base, disp); break;
    case LOAD_LINKED_LONG:
      ll_d(reg, base, disp);
      break;
    default:
      ShouldNotReachHere();
  }
}

void MacroAssembler::loadstore(Register reg, Register base, Register disp, int type) {
  switch (type) {
    case STORE_BYTE:   stx_b (reg, base, disp); break;
    case STORE_CHAR:
    case STORE_SHORT:  stx_h (reg, base, disp); break;
    case STORE_INT:    stx_w (reg, base, disp); break;
    case STORE_LONG:   stx_d (reg, base, disp); break;
    case LOAD_BYTE:    ldx_b (reg, base, disp); break;
    case LOAD_U_BYTE:  ldx_bu(reg, base, disp); break;
    case LOAD_SHORT:   ldx_h (reg, base, disp); break;
    case LOAD_U_SHORT: ldx_hu(reg, base, disp); break;
    case LOAD_INT:     ldx_w (reg, base, disp); break;
    case LOAD_U_INT:   ldx_wu(reg, base, disp); break;
    case LOAD_LONG:    ldx_d (reg, base, disp); break;
    case LOAD_LINKED_LONG:
      add_d(AT, base, disp);
      ll_d(reg, AT, 0);
      break;
    default:
      ShouldNotReachHere();
  }
}

void MacroAssembler::loadstore(FloatRegister reg, Register base, int disp, int type) {
  switch (type) {
    case STORE_FLOAT:    fst_s(reg, base, disp); break;
    case STORE_DOUBLE:   fst_d(reg, base, disp); break;
    case STORE_VECTORX:  vst  (reg, base, disp); break;
    case STORE_VECTORY: xvst  (reg, base, disp); break;
    case LOAD_FLOAT:     fld_s(reg, base, disp); break;
    case LOAD_DOUBLE:    fld_d(reg, base, disp); break;
    case LOAD_VECTORX:   vld  (reg, base, disp); break;
    case LOAD_VECTORY:  xvld  (reg, base, disp); break;
    default:
      ShouldNotReachHere();
  }
}

void MacroAssembler::loadstore(FloatRegister reg, Register base, Register disp, int type) {
  switch (type) {
    case STORE_FLOAT:    fstx_s(reg, base, disp); break;
    case STORE_DOUBLE:   fstx_d(reg, base, disp); break;
    case STORE_VECTORX:  vstx  (reg, base, disp); break;
    case STORE_VECTORY: xvstx  (reg, base, disp); break;
    case LOAD_FLOAT:     fldx_s(reg, base, disp); break;
    case LOAD_DOUBLE:    fldx_d(reg, base, disp); break;
    case LOAD_VECTORX:   vldx  (reg, base, disp); break;
    case LOAD_VECTORY:  xvldx  (reg, base, disp); break;
    default:
      ShouldNotReachHere();
  }
}

#ifdef COMPILER2
void MacroAssembler::reduce_ins_v(FloatRegister vec1, FloatRegister vec2, FloatRegister vec3, BasicType type, int opcode) {
  switch (type) {
    case T_BYTE:
      switch (opcode) {
        case Op_AddReductionVI: vadd_b(vec1, vec2, vec3); break;
        case Op_MulReductionVI: vmul_b(vec1, vec2, vec3); break;
        case Op_MaxReductionV:  vmax_b(vec1, vec2, vec3); break;
        case Op_MinReductionV:  vmin_b(vec1, vec2, vec3); break;
        default:
          ShouldNotReachHere();
      }
      break;
    case T_SHORT:
      switch (opcode) {
        case Op_AddReductionVI: vadd_h(vec1, vec2, vec3); break;
        case Op_MulReductionVI: vmul_h(vec1, vec2, vec3); break;
        case Op_MaxReductionV:  vmax_h(vec1, vec2, vec3); break;
        case Op_MinReductionV:  vmin_h(vec1, vec2, vec3); break;
        default:
          ShouldNotReachHere();
      }
      break;
    case T_INT:
      switch (opcode) {
        case Op_AddReductionVI: vadd_w(vec1, vec2, vec3); break;
        case Op_MulReductionVI: vmul_w(vec1, vec2, vec3); break;
        case Op_MaxReductionV:  vmax_w(vec1, vec2, vec3); break;
        case Op_MinReductionV:  vmin_w(vec1, vec2, vec3); break;
        default:
          ShouldNotReachHere();
      }
      break;
    case T_LONG:
      switch (opcode) {
        case Op_AddReductionVL: vadd_d(vec1, vec2, vec3); break;
        case Op_MulReductionVL: vmul_d(vec1, vec2, vec3); break;
        case Op_MaxReductionV:  vmax_d(vec1, vec2, vec3); break;
        case Op_MinReductionV:  vmin_d(vec1, vec2, vec3); break;
        default:
          ShouldNotReachHere();
      }
      break;
    default:
      ShouldNotReachHere();
  }
}

void MacroAssembler::reduce_ins_r(Register reg1, Register reg2, Register reg3, BasicType type, int opcode) {
  switch (type) {
    case T_BYTE:
    case T_SHORT:
    case T_INT:
      switch (opcode) {
        case Op_AddReductionVI: add_w(reg1, reg2, reg3); break;
        case Op_MulReductionVI: mul_w(reg1, reg2, reg3); break;
        default:
          ShouldNotReachHere();
      }
      break;
    case T_LONG:
      switch (opcode) {
        case Op_AddReductionVL: add_d(reg1, reg2, reg3); break;
        case Op_MulReductionVL: mul_d(reg1, reg2, reg3); break;
        default:
          ShouldNotReachHere();
      }
      break;
    default:
      ShouldNotReachHere();
  }
}

void MacroAssembler::reduce_ins_f(FloatRegister reg1, FloatRegister reg2, FloatRegister reg3, BasicType type, int opcode) {
  switch (type) {
    case T_FLOAT:
      switch (opcode) {
        case Op_AddReductionVF: fadd_s(reg1, reg2, reg3); break;
        case Op_MulReductionVF: fmul_s(reg1, reg2, reg3); break;
        default:
          ShouldNotReachHere();
      }
      break;
    case T_DOUBLE:
      switch (opcode) {
        case Op_AddReductionVD: fadd_d(reg1, reg2, reg3); break;
        case Op_MulReductionVD: fmul_d(reg1, reg2, reg3); break;
        default:
          ShouldNotReachHere();
      }
      break;
    default:
      ShouldNotReachHere();
  }
}

void MacroAssembler::reduce(Register dst, Register src, FloatRegister vsrc, FloatRegister tmp1, FloatRegister tmp2, BasicType type, int opcode, int vector_size) {
  if (vector_size == 32) {
    xvpermi_d(tmp1, vsrc, 0b00001110);
    reduce_ins_v(tmp1, vsrc, tmp1, type, opcode);
    vpermi_w(tmp2, tmp1, 0b00001110);
    reduce_ins_v(tmp1, tmp2, tmp1, type, opcode);
  } else if (vector_size == 16) {
    vpermi_w(tmp1, vsrc, 0b00001110);
    reduce_ins_v(tmp1, vsrc, tmp1, type, opcode);
  } else {
    ShouldNotReachHere();
  }

  if (type != T_LONG) {
    vshuf4i_w(tmp2, tmp1, 0b00000001);
    reduce_ins_v(tmp1, tmp2, tmp1, type, opcode);
    if (type != T_INT) {
      vshuf4i_h(tmp2, tmp1, 0b00000001);
      reduce_ins_v(tmp1, tmp2, tmp1, type, opcode);
      if (type != T_SHORT) {
        vshuf4i_b(tmp2, tmp1, 0b00000001);
        reduce_ins_v(tmp1, tmp2, tmp1, type, opcode);
      }
    }
  }

  switch (type) {
    case T_BYTE:  vpickve2gr_b(dst, tmp1, 0); break;
    case T_SHORT: vpickve2gr_h(dst, tmp1, 0); break;
    case T_INT:   vpickve2gr_w(dst, tmp1, 0); break;
    case T_LONG:  vpickve2gr_d(dst, tmp1, 0); break;
    default:
      ShouldNotReachHere();
  }
  if (opcode == Op_MaxReductionV) {
    slt(AT, dst, src);
    masknez(dst, dst, AT);
    maskeqz(AT, src, AT);
    orr(dst, dst, AT);
  } else if (opcode == Op_MinReductionV) {
    slt(AT, src, dst);
    masknez(dst, dst, AT);
    maskeqz(AT, src, AT);
    orr(dst, dst, AT);
  } else {
    reduce_ins_r(dst, dst, src, type, opcode);
  }
  switch (type) {
    case T_BYTE:  ext_w_b(dst, dst); break;
    case T_SHORT: ext_w_h(dst, dst); break;
    default:
      break;
  }
}

void MacroAssembler::reduce(FloatRegister dst, FloatRegister src, FloatRegister vsrc, FloatRegister tmp, BasicType type, int opcode, int vector_size) {
  if (vector_size == 32) {
    switch (type) {
      case T_FLOAT:
        reduce_ins_f(dst, vsrc, src, type, opcode);
        xvpickve_w(tmp, vsrc, 1);
        reduce_ins_f(dst, tmp, dst, type, opcode);
        xvpickve_w(tmp, vsrc, 2);
        reduce_ins_f(dst, tmp, dst, type, opcode);
        xvpickve_w(tmp, vsrc, 3);
        reduce_ins_f(dst, tmp, dst, type, opcode);
        xvpickve_w(tmp, vsrc, 4);
        reduce_ins_f(dst, tmp, dst, type, opcode);
        xvpickve_w(tmp, vsrc, 5);
        reduce_ins_f(dst, tmp, dst, type, opcode);
        xvpickve_w(tmp, vsrc, 6);
        reduce_ins_f(dst, tmp, dst, type, opcode);
        xvpickve_w(tmp, vsrc, 7);
        reduce_ins_f(dst, tmp, dst, type, opcode);
        break;
      case T_DOUBLE:
        reduce_ins_f(dst, vsrc, src, type, opcode);
        xvpickve_d(tmp, vsrc, 1);
        reduce_ins_f(dst, tmp, dst, type, opcode);
        xvpickve_d(tmp, vsrc, 2);
        reduce_ins_f(dst, tmp, dst, type, opcode);
        xvpickve_d(tmp, vsrc, 3);
        reduce_ins_f(dst, tmp, dst, type, opcode);
        break;
      default:
        ShouldNotReachHere();
    }
  } else if (vector_size == 16) {
    switch (type) {
      case T_FLOAT:
        reduce_ins_f(dst, vsrc, src, type, opcode);
        vpermi_w(tmp, vsrc, 0b00000001);
        reduce_ins_f(dst, tmp, dst, type, opcode);
        vpermi_w(tmp, vsrc, 0b00000010);
        reduce_ins_f(dst, tmp, dst, type, opcode);
        vpermi_w(tmp, vsrc, 0b00000011);
        reduce_ins_f(dst, tmp, dst, type, opcode);
        break;
      case T_DOUBLE:
        reduce_ins_f(dst, vsrc, src, type, opcode);
        vpermi_w(tmp, vsrc, 0b00001110);
        reduce_ins_f(dst, tmp, dst, type, opcode);
        break;
      default:
        ShouldNotReachHere();
    }
  } else {
    ShouldNotReachHere();
  }
}
#endif // COMPILER2

/**
 * Emits code to update CRC-32 with a byte value according to constants in table
 *
 * @param [in,out]crc   Register containing the crc.
 * @param [in]val       Register containing the byte to fold into the CRC.
 * @param [in]table     Register containing the table of crc constants.
 *
 * uint32_t crc;
 * val = crc_table[(val ^ crc) & 0xFF];
 * crc = val ^ (crc >> 8);
**/
void MacroAssembler::update_byte_crc32(Register crc, Register val, Register table) {
  xorr(val, val, crc);
  andi(val, val, 0xff);
  ld_w(val, Address(table, val, Address::times_4, 0));
  srli_w(crc, crc, 8);
  xorr(crc, val, crc);
}

/**
 * @param crc   register containing existing CRC (32-bit)
 * @param buf   register pointing to input byte buffer (byte*)
 * @param len   register containing number of bytes
 * @param tmp   scratch register
**/
void MacroAssembler::kernel_crc32(Register crc, Register buf, Register len, Register tmp) {
  Label CRC_by64_loop, CRC_by4_loop, CRC_by1_loop, CRC_less64, CRC_by64_pre, CRC_by32_loop, CRC_less32, L_exit;
  assert_different_registers(crc, buf, len, tmp);

    nor(crc, crc, R0);

    addi_d(len, len, -64);
    bge(len, R0, CRC_by64_loop);
    addi_d(len, len, 64-4);
    bge(len, R0, CRC_by4_loop);
    addi_d(len, len, 4);
    blt(R0, len, CRC_by1_loop);
    b(L_exit);

  bind(CRC_by64_loop);
    ld_d(tmp, buf, 0);
    crc_w_d_w(crc, tmp, crc);
    ld_d(tmp, buf, 8);
    crc_w_d_w(crc, tmp, crc);
    ld_d(tmp, buf, 16);
    crc_w_d_w(crc, tmp, crc);
    ld_d(tmp, buf, 24);
    crc_w_d_w(crc, tmp, crc);
    ld_d(tmp, buf, 32);
    crc_w_d_w(crc, tmp, crc);
    ld_d(tmp, buf, 40);
    crc_w_d_w(crc, tmp, crc);
    ld_d(tmp, buf, 48);
    crc_w_d_w(crc, tmp, crc);
    ld_d(tmp, buf, 56);
    crc_w_d_w(crc, tmp, crc);
    addi_d(buf, buf, 64);
    addi_d(len, len, -64);
    bge(len, R0, CRC_by64_loop);
    addi_d(len, len, 64-4);
    bge(len, R0, CRC_by4_loop);
    addi_d(len, len, 4);
    blt(R0, len, CRC_by1_loop);
    b(L_exit);

  bind(CRC_by4_loop);
    ld_w(tmp, buf, 0);
    crc_w_w_w(crc, tmp, crc);
    addi_d(buf, buf, 4);
    addi_d(len, len, -4);
    bge(len, R0, CRC_by4_loop);
    addi_d(len, len, 4);
    bge(R0, len, L_exit);

  bind(CRC_by1_loop);
    ld_b(tmp, buf, 0);
    crc_w_b_w(crc, tmp, crc);
    addi_d(buf, buf, 1);
    addi_d(len, len, -1);
    blt(R0, len, CRC_by1_loop);

  bind(L_exit);
    nor(crc, crc, R0);
}

/**
 * @param crc   register containing existing CRC (32-bit)
 * @param buf   register pointing to input byte buffer (byte*)
 * @param len   register containing number of bytes
 * @param tmp   scratch register
**/
void MacroAssembler::kernel_crc32c(Register crc, Register buf, Register len, Register tmp) {
  Label CRC_by64_loop, CRC_by4_loop, CRC_by1_loop, CRC_less64, CRC_by64_pre, CRC_by32_loop, CRC_less32, L_exit;
  assert_different_registers(crc, buf, len, tmp);

    addi_d(len, len, -64);
    bge(len, R0, CRC_by64_loop);
    addi_d(len, len, 64-4);
    bge(len, R0, CRC_by4_loop);
    addi_d(len, len, 4);
    blt(R0, len, CRC_by1_loop);
    b(L_exit);

  bind(CRC_by64_loop);
    ld_d(tmp, buf, 0);
    crcc_w_d_w(crc, tmp, crc);
    ld_d(tmp, buf, 8);
    crcc_w_d_w(crc, tmp, crc);
    ld_d(tmp, buf, 16);
    crcc_w_d_w(crc, tmp, crc);
    ld_d(tmp, buf, 24);
    crcc_w_d_w(crc, tmp, crc);
    ld_d(tmp, buf, 32);
    crcc_w_d_w(crc, tmp, crc);
    ld_d(tmp, buf, 40);
    crcc_w_d_w(crc, tmp, crc);
    ld_d(tmp, buf, 48);
    crcc_w_d_w(crc, tmp, crc);
    ld_d(tmp, buf, 56);
    crcc_w_d_w(crc, tmp, crc);
    addi_d(buf, buf, 64);
    addi_d(len, len, -64);
    bge(len, R0, CRC_by64_loop);
    addi_d(len, len, 64-4);
    bge(len, R0, CRC_by4_loop);
    addi_d(len, len, 4);
    blt(R0, len, CRC_by1_loop);
    b(L_exit);

  bind(CRC_by4_loop);
    ld_w(tmp, buf, 0);
    crcc_w_w_w(crc, tmp, crc);
    addi_d(buf, buf, 4);
    addi_d(len, len, -4);
    bge(len, R0, CRC_by4_loop);
    addi_d(len, len, 4);
    bge(R0, len, L_exit);

  bind(CRC_by1_loop);
    ld_b(tmp, buf, 0);
    crcc_w_b_w(crc, tmp, crc);
    addi_d(buf, buf, 1);
    addi_d(len, len, -1);
    blt(R0, len, CRC_by1_loop);

  bind(L_exit);
}

#ifdef COMPILER2
void MacroAssembler::cmp_branch_short(int flag, Register op1, Register op2, Label& L, bool is_signed) {

    switch(flag) {
      case 0x01: //equal
          beq(op1, op2, L);
        break;
      case 0x02: //not_equal
          bne(op1, op2, L);
        break;
      case 0x03: //above
        if (is_signed)
          blt(op2, op1, L);
        else
          bltu(op2, op1, L);
        break;
      case 0x04: //above_equal
        if (is_signed)
          bge(op1, op2, L);
        else
          bgeu(op1, op2, L);
        break;
      case 0x05: //below
        if (is_signed)
          blt(op1, op2, L);
        else
          bltu(op1, op2, L);
        break;
      case 0x06: //below_equal
        if (is_signed)
          bge(op2, op1, L);
        else
          bgeu(op2, op1, L);
        break;
      default:
        Unimplemented();
    }
}

void MacroAssembler::cmp_branch_long(int flag, Register op1, Register op2, Label* L, bool is_signed) {
    switch(flag) {
      case 0x01: //equal
        beq_long(op1, op2, *L);
        break;
      case 0x02: //not_equal
        bne_long(op1, op2, *L);
        break;
      case 0x03: //above
        if (is_signed)
          blt_long(op2, op1, *L, true /* signed */);
        else
          blt_long(op2, op1, *L, false);
        break;
      case 0x04: //above_equal
        if (is_signed)
          bge_long(op1, op2, *L, true /* signed */);
        else
          bge_long(op1, op2, *L, false);
        break;
      case 0x05: //below
        if (is_signed)
          blt_long(op1, op2, *L, true /* signed */);
        else
          blt_long(op1, op2, *L, false);
        break;
      case 0x06: //below_equal
        if (is_signed)
          bge_long(op2, op1, *L, true /* signed */);
        else
          bge_long(op2, op1, *L, false);
        break;
      default:
        Unimplemented();
    }
}

void MacroAssembler::cmp_branchEqNe_off21(int flag, Register op1, Label& L) {
    switch(flag) {
      case 0x01: //equal
        beqz(op1, L);
        break;
      case 0x02: //not_equal
        bnez(op1, L);
        break;
      default:
        Unimplemented();
    }
}
#endif // COMPILER2

void MacroAssembler::membar(Membar_mask_bits hint){
  address prev = pc() - NativeInstruction::sync_instruction_size;
  address last = code()->last_insn();
  if (last != NULL && ((NativeInstruction*)last)->is_sync() && prev == last) {
    code()->set_last_insn(NULL);
    block_comment("merged membar");
  } else {
    code()->set_last_insn(pc());
    dbar(hint);
  }
}

// Code for BigInteger::mulAdd intrinsic
// out     = A0
// in      = A1
// offset  = A2  (already out.length-offset)
// len     = A3
// k       = A4
//
// pseudo code from java implementation:
// long kLong = k & LONG_MASK;
// carry = 0;
// offset = out.length-offset - 1;
// for (int j = len - 1; j >= 0; j--) {
//     product = (in[j] & LONG_MASK) * kLong + (out[offset] & LONG_MASK) + carry;
//     out[offset--] = (int)product;
//     carry = product >>> 32;
// }
// return (int)carry;
void MacroAssembler::mul_add(Register out, Register in, Register offset,
                             Register len, Register k) {
  Label L_tail_loop, L_unroll, L_end;

  move(SCR2, out);
  move(out, R0); // should clear out
  bge(R0, len, L_end);

  alsl_d(offset, offset, SCR2, LogBytesPerInt - 1);
  alsl_d(in, len, in, LogBytesPerInt - 1);

  const int unroll = 16;
  li(SCR2, unroll);
  blt(len, SCR2, L_tail_loop);

  bind(L_unroll);

    addi_d(in, in, -unroll * BytesPerInt);
    addi_d(offset, offset, -unroll * BytesPerInt);

    for (int i = unroll - 1; i >= 0; i--) {
      ld_wu(SCR1, in, i * BytesPerInt);
      mulw_d_wu(SCR1, SCR1, k);
      add_d(out, out, SCR1); // out as scratch
      ld_wu(SCR1, offset, i * BytesPerInt);
      add_d(SCR1, SCR1, out);
      st_w(SCR1, offset, i * BytesPerInt);
      srli_d(out, SCR1, 32); // keep carry
    }

    sub_w(len, len, SCR2);
    bge(len, SCR2, L_unroll);

  bge(R0, len, L_end); // check tail

  bind(L_tail_loop);

    addi_d(in, in, -BytesPerInt);
    ld_wu(SCR1, in, 0);
    mulw_d_wu(SCR1, SCR1, k);
    add_d(out, out, SCR1); // out as scratch

    addi_d(offset, offset, -BytesPerInt);
    ld_wu(SCR1, offset, 0);
    add_d(SCR1, SCR1, out);
    st_w(SCR1, offset, 0);

    srli_d(out, SCR1, 32); // keep carry

    addi_w(len, len, -1);
    blt(R0, len, L_tail_loop);

  bind(L_end);
}

