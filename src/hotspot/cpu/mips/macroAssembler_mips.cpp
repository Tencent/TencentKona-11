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
#include "nativeInst_mips.hpp"
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
#include "opto/intrinsicnode.hpp"
#endif

#define T0 RT0
#define T1 RT1
#define T2 RT2
#define T3 RT3
#define T8 RT8
#define T9 RT9

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
    __ sw (as_Register(k), A0, i_offset(k));
  }

  for(int k=0; k<32; k++) {
    __ swc1 (as_FloatRegister(k), A0, f_offset(k));
  }
#undef __
}

void MacroAssembler::restore_registers(MacroAssembler *masm) {
#define __ masm->
  for(int k=0; k<32; k++) {
    __ lw (as_Register(k), A0, i_offset(k));
  }

  for(int k=0; k<32; k++) {
    __ lwc1 (as_FloatRegister(k), A0, f_offset(k));
  }
#undef __
}


void MacroAssembler::pd_patch_instruction(address branch, address target) {
  jint& stub_inst = *(jint*) branch;
  jint *pc = (jint *)branch;

  if((opcode(stub_inst) == special_op) && (special(stub_inst) == daddu_op)) {
    //b_far:
    //  move(AT, RA); // daddu
    //  emit_long(insn_ORRI(regimm_op, 0, bgezal_op, 1));
    //  nop();
    //  lui(T9, 0); // to be patched
    //  ori(T9, 0);
    //  daddu(T9, T9, RA);
    //  move(RA, AT);
    //  jr(T9);

    assert(opcode(pc[3]) == lui_op
        && opcode(pc[4]) == ori_op
        && special(pc[5]) == daddu_op, "Not a branch label patch");
    if(!(opcode(pc[3]) == lui_op
          && opcode(pc[4]) == ori_op
          && special(pc[5]) == daddu_op)) { tty->print_cr("Not a branch label patch"); }

    int offset = target - branch;
    if (!is_simm16(offset)) {
      pc[3] = (pc[3] & 0xffff0000) | high16(offset - 12);
      pc[4] = (pc[4] & 0xffff0000) | low16(offset - 12);
    } else {
      // revert to "beq + nop"
      CodeBuffer cb(branch, 4 * 10);
      MacroAssembler masm(&cb);
#define __ masm.
      __ b(target);
      __ delayed()->nop();
      __ nop();
      __ nop();
      __ nop();
      __ nop();
      __ nop();
      __ nop();
    }
    return;
  } else if (special(pc[4]) == jr_op
             && opcode(pc[4]) == special_op
             && (((opcode(pc[0]) == lui_op) || opcode(pc[0]) == daddiu_op) || (opcode(pc[0]) == ori_op))) {
    //jmp_far:
    //  patchable_set48(T9, target);
    //  jr(T9);
    //  nop();

    CodeBuffer cb(branch, 4 * 4);
    MacroAssembler masm(&cb);
    masm.patchable_set48(T9, (long)(target));
    return;
  }

#ifndef PRODUCT
  if (!is_simm16((target - branch - 4) >> 2)) {
    tty->print_cr("Illegal patching: branch = " INTPTR_FORMAT ", target = " INTPTR_FORMAT, p2i(branch), p2i(target));
    tty->print_cr("======= Start decoding at branch = " INTPTR_FORMAT " =======", p2i(branch));
    Disassembler::decode(branch - 4 * 16, branch + 4 * 16, tty);
    tty->print_cr("======= End of decoding =======");
  }
#endif

  stub_inst = patched_branch(target - branch, stub_inst, 0);
}

static inline address first_cache_address() {
  return CodeCache::low_bound() + sizeof(HeapBlock::Header);
}

static inline address last_cache_address() {
  return CodeCache::high_bound() - Assembler::InstructionSize;
}

int MacroAssembler::call_size(address target, bool far, bool patchable) {
  if (patchable) return 6 << Assembler::LogInstructionSize;
  if (!far) return 2 << Assembler::LogInstructionSize; // jal + nop
  return (insts_for_set64((jlong)target) + 2) << Assembler::LogInstructionSize;
}

// Can we reach target using jal/j from anywhere
// in the code cache (because code can be relocated)?
bool MacroAssembler::reachable_from_cache(address target) {
  address cl = first_cache_address();
  address ch = last_cache_address();

  return (cl <= target) && (target <= ch) && fit_in_jal(cl, ch);
}

bool MacroAssembler::reachable_from_cache() {
  if (ForceUnreachable) {
    return false;
  } else {
    address cl = first_cache_address();
    address ch = last_cache_address();

    return fit_in_jal(cl, ch);
  }
}

void MacroAssembler::general_jump(address target) {
  if (reachable_from_cache(target)) {
    j(target);
    delayed()->nop();
  } else {
    set64(T9, (long)target);
    jr(T9);
    delayed()->nop();
  }
}

int MacroAssembler::insts_for_general_jump(address target) {
  if (reachable_from_cache(target)) {
    //j(target);
    //nop();
    return 2;
  } else {
    //set64(T9, (long)target);
    //jr(T9);
    //nop();
    return insts_for_set64((jlong)target) + 2;
  }
}

void MacroAssembler::patchable_jump(address target) {
  if (reachable_from_cache(target)) {
    nop();
    nop();
    nop();
    nop();
    j(target);
    delayed()->nop();
  } else {
    patchable_set48(T9, (long)target);
    jr(T9);
    delayed()->nop();
  }
}

int MacroAssembler::insts_for_patchable_jump(address target) {
  return 6;
}

void MacroAssembler::general_call(address target) {
  if (reachable_from_cache(target)) {
    jal(target);
    delayed()->nop();
  } else {
    set64(T9, (long)target);
    jalr(T9);
    delayed()->nop();
  }
}

int MacroAssembler::insts_for_general_call(address target) {
  if (reachable_from_cache(target)) {
    //jal(target);
    //nop();
    return 2;
  } else {
    //set64(T9, (long)target);
    //jalr(T9);
    //nop();
    return insts_for_set64((jlong)target) + 2;
  }
}

void MacroAssembler::patchable_call(address target) {
  if (reachable_from_cache(target)) {
    nop();
    nop();
    nop();
    nop();
    jal(target);
    delayed()->nop();
  } else {
    patchable_set48(T9, (long)target);
    jalr(T9);
    delayed()->nop();
  }
}

int MacroAssembler::insts_for_patchable_call(address target) {
  return 6;
}

// Maybe emit a call via a trampoline.  If the code cache is small
// trampolines won't be emitted.

address MacroAssembler::trampoline_call(AddressLiteral entry, CodeBuffer *cbuf) {
  assert(JavaThread::current()->is_Compiler_thread(), "just checking");
  assert(entry.rspec().type() == relocInfo::runtime_call_type
         || entry.rspec().type() == relocInfo::opt_virtual_call_type
         || entry.rspec().type() == relocInfo::static_call_type
         || entry.rspec().type() == relocInfo::virtual_call_type, "wrong reloc type");

  address target = entry.target();
  if (!reachable_from_cache()) {
    address stub = emit_trampoline_stub(offset(), target);
    if (stub == NULL) {
      return NULL; // CodeCache is full
    }
  }

  if (cbuf) cbuf->set_insts_mark();
  relocate(entry.rspec());

  if (reachable_from_cache()) {
    nop();
    nop();
    nop();
    nop();
    jal(target);
    delayed()->nop();
  } else {
    // load the call target from the trampoline stub
    // branch
    long dest = (long)pc();
    dest += (dest & 0x8000) << 1;
    lui(T9, dest >> 32);
    ori(T9, T9, split_low(dest >> 16));
    dsll(T9, T9, 16);
    ld(T9, T9, simm16(split_low(dest)));
    jalr(T9);
    delayed()->nop();
  }
  return pc();
}

// Emit a trampoline stub for a call to a target which is too far away.
address MacroAssembler::emit_trampoline_stub(int insts_call_instruction_offset,
                                             address dest) {
  // Max stub size: alignment nop, TrampolineStub.
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
  emit_int64((int64_t)dest);
  end_a_stub();
  return stub;
}

void MacroAssembler::beq_far(Register rs, Register rt, address entry) {
  u_char * cur_pc = pc();

  // Near/Far jump
  if(is_simm16((entry - pc() - 4) / 4)) {
    Assembler::beq(rs, rt, offset(entry));
  } else {
    Label not_jump;
    bne(rs, rt, not_jump);
    delayed()->nop();

    b_far(entry);
    delayed()->nop();

    bind(not_jump);
    has_delay_slot();
  }
}

void MacroAssembler::beq_far(Register rs, Register rt, Label& L) {
  if (L.is_bound()) {
    beq_far(rs, rt, target(L));
  } else {
    u_char * cur_pc = pc();
    Label not_jump;
    bne(rs, rt, not_jump);
    delayed()->nop();

    b_far(L);
    delayed()->nop();

    bind(not_jump);
    has_delay_slot();
  }
}

void MacroAssembler::bne_far(Register rs, Register rt, address entry) {
  u_char * cur_pc = pc();

  //Near/Far jump
  if(is_simm16((entry - pc() - 4) / 4)) {
    Assembler::bne(rs, rt, offset(entry));
  } else {
    Label not_jump;
    beq(rs, rt, not_jump);
    delayed()->nop();

    b_far(entry);
    delayed()->nop();

    bind(not_jump);
    has_delay_slot();
  }
}

void MacroAssembler::bne_far(Register rs, Register rt, Label& L) {
  if (L.is_bound()) {
    bne_far(rs, rt, target(L));
  } else {
    u_char * cur_pc = pc();
    Label not_jump;
    beq(rs, rt, not_jump);
    delayed()->nop();

    b_far(L);
    delayed()->nop();

    bind(not_jump);
    has_delay_slot();
  }
}

void MacroAssembler::beq_long(Register rs, Register rt, Label& L) {
  Label not_taken;

  bne(rs, rt, not_taken);
  delayed()->nop();

  jmp_far(L);

  bind(not_taken);
}

void MacroAssembler::bne_long(Register rs, Register rt, Label& L) {
  Label not_taken;

  beq(rs, rt, not_taken);
  delayed()->nop();

  jmp_far(L);

  bind(not_taken);
}

void MacroAssembler::bc1t_long(Label& L) {
  Label not_taken;

  bc1f(not_taken);
  delayed()->nop();

  jmp_far(L);

  bind(not_taken);
}

void MacroAssembler::bc1f_long(Label& L) {
  Label not_taken;

  bc1t(not_taken);
  delayed()->nop();

  jmp_far(L);

  bind(not_taken);
}

void MacroAssembler::b_far(Label& L) {
  if (L.is_bound()) {
    b_far(target(L));
  } else {
    volatile address dest = target(L);
//
// MacroAssembler::pd_patch_instruction branch=55651ed514, target=55651ef6d8
//   0x00000055651ed514: daddu at, ra, zero
//   0x00000055651ed518: [4110001]bgezal zero, 0x00000055651ed520
//
//   0x00000055651ed51c: sll zero, zero, 0
//   0x00000055651ed520: lui t9, 0x0
//   0x00000055651ed524: ori t9, t9, 0x21b8
//   0x00000055651ed528: daddu t9, t9, ra
//   0x00000055651ed52c: daddu ra, at, zero
//   0x00000055651ed530: jr t9
//   0x00000055651ed534: sll zero, zero, 0
//
    move(AT, RA);
    emit_long(insn_ORRI(regimm_op, 0, bgezal_op, 1));
    nop();
    lui(T9, 0); // to be patched
    ori(T9, T9, 0);
    daddu(T9, T9, RA);
    move(RA, AT);
    jr(T9);
  }
}

void MacroAssembler::b_far(address entry) {
  u_char * cur_pc = pc();

  // Near/Far jump
  if(is_simm16((entry - pc() - 4) / 4)) {
    b(offset(entry));
  } else {
    // address must be bounded
    move(AT, RA);
    emit_long(insn_ORRI(regimm_op, 0, bgezal_op, 1));
    nop();
    li32(T9, entry - pc());
    daddu(T9, T9, RA);
    move(RA, AT);
    jr(T9);
  }
}

void MacroAssembler::ld_ptr(Register rt, Register base, Register offset) {
  addu_long(AT, base, offset);
  ld_ptr(rt, AT, 0);
}

void MacroAssembler::st_ptr(Register rt, Register base, Register offset) {
  guarantee(AT != rt, "AT must not equal rt");
  addu_long(AT, base, offset);
  st_ptr(rt, AT, 0);
}

Address MacroAssembler::as_Address(AddressLiteral adr) {
  return Address(adr.target(), adr.rspec());
}

Address MacroAssembler::as_Address(ArrayAddress adr) {
  return Address::make_array(adr);
}

// tmp_reg1 and tmp_reg2 should be saved outside of atomic_inc32 (caller saved).
void MacroAssembler::atomic_inc32(address counter_addr, int inc, Register tmp_reg1, Register tmp_reg2) {
  Label again;

  li(tmp_reg1, counter_addr);
  bind(again);
  if (UseSyncLevel >= 10000 || UseSyncLevel == 1000 || UseSyncLevel == 4000) sync();
  ll(tmp_reg2, tmp_reg1, 0);
  addiu(tmp_reg2, tmp_reg2, inc);
  sc(tmp_reg2, tmp_reg1, 0);
  beq(tmp_reg2, R0, again);
  delayed()->nop();
}

void MacroAssembler::reserved_stack_check() {
  Register thread = TREG;
#ifndef OPT_THREAD
  get_thread(thread);
#endif
  // testing if reserved zone needs to be enabled
  Label no_reserved_zone_enabling;

  ld(AT, Address(thread, JavaThread::reserved_stack_activation_offset()));
  dsubu(AT, SP, AT);
  bltz(AT, no_reserved_zone_enabling);
  delayed()->nop();

  enter();   // RA and FP are live.
  call_VM_leaf(CAST_FROM_FN_PTR(address, SharedRuntime::enable_stack_reserved_zone), thread);
  leave();

  // We have already removed our own frame.
  // throw_delayed_StackOverflowError will think that it's been
  // called by our caller.
  li(AT, (long)StubRoutines::throw_delayed_StackOverflowError_entry());
  jr(AT);
  delayed()->nop();
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
    tmp_reg = T9;
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
  daddiu(AT, R0, markOopDesc::biased_lock_pattern);
  dsubu(AT, AT, tmp_reg);
  if (need_tmp_reg) {
    pop(tmp_reg);
  }

  bne(AT, R0, cas_label);
  delayed()->nop();


  // The bias pattern is present in the object's header. Need to check
  // whether the bias owner and the epoch are both still current.
  // Note that because there is no current thread register on MIPS we
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

  move(AT, ~((int) markOopDesc::age_mask_in_place));
  andr(swap_reg, swap_reg, AT);

  if (PrintBiasedLockingStatistics) {
    Label L;
    bne(swap_reg, R0, L);
    delayed()->nop();
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
  delayed()->nop();
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

  move(AT, markOopDesc::biased_lock_mask_in_place);
  andr(AT, swap_reg, AT);
  bne(AT, R0, try_revoke_bias);
  delayed()->nop();
  // Biasing is still enabled for this data type. See whether the
  // epoch of the current bias is still valid, meaning that the epoch
  // bits of the mark word are equal to the epoch bits of the
  // prototype header. (Note that the prototype header's epoch bits
  // only change at a safepoint.) If not, attempt to rebias the object
  // toward the current thread. Note that we must be absolutely sure
  // that the current epoch is invalid in order to do this because
  // otherwise the manipulations it performs on the mark word are
  // illegal.

  move(AT, markOopDesc::epoch_mask_in_place);
  andr(AT,swap_reg, AT);
  bne(AT, R0, try_rebias);
  delayed()->nop();
  // The epoch of the current bias is still valid but we know nothing
  // about the owner; it might be set or it might be clear. Try to
  // acquire the bias of the object using an atomic operation. If this
  // fails we will go in to the runtime to revoke the object's bias.
  // Note that we first construct the presumed unbiased header so we
  // don't accidentally blow away another thread's valid bias.

  ld_ptr(swap_reg, saved_mark_addr);

  move(AT, markOopDesc::biased_lock_mask_in_place | markOopDesc::age_mask_in_place | markOopDesc::epoch_mask_in_place);
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
    delayed()->nop();
    push(tmp_reg);
    push(A0);
    atomic_inc32((address)BiasedLocking::anonymously_biased_lock_entry_count_addr(), 1, A0, tmp_reg);
    pop(A0);
    pop(tmp_reg);
    bind(L);
  }
  if (slow_case != NULL) {
    beq_far(AT, R0, *slow_case);
    delayed()->nop();
  }
  b(done);
  delayed()->nop();

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
    delayed()->nop();
    push(AT);
    push(tmp_reg);
    atomic_inc32((address)BiasedLocking::rebiased_lock_entry_count_addr(), 1, AT, tmp_reg);
    pop(tmp_reg);
    pop(AT);
    bind(L);
  }
  if (slow_case != NULL) {
    beq_far(AT, R0, *slow_case);
    delayed()->nop();
  }

  b(done);
  delayed()->nop();
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
    delayed()->nop();
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
  ld(temp_reg, Address(obj_reg, oopDesc::mark_offset_in_bytes()));
  andi(temp_reg, temp_reg, markOopDesc::biased_lock_mask_in_place);
  daddiu(AT, R0, markOopDesc::biased_lock_pattern);

  beq(AT, temp_reg, done);
  delayed()->nop();
}

// the stack pointer adjustment is needed. see InterpreterMacroAssembler::super_call_VM_leaf
// this method will handle the stack problem, you need not to preserve the stack space for the argument now
void MacroAssembler::call_VM_leaf_base(address entry_point, int number_of_arguments) {
  Label L, E;

  assert(number_of_arguments <= 4, "just check");

  andi(AT, SP, 0xf);
  beq(AT, R0, L);
  delayed()->nop();
  daddiu(SP, SP, -8);
  call(entry_point, relocInfo::runtime_call_type);
  delayed()->nop();
  daddiu(SP, SP, 8);
  b(E);
  delayed()->nop();

  bind(L);
  call(entry_point, relocInfo::runtime_call_type);
  delayed()->nop();
  bind(E);
}


void MacroAssembler::jmp(address entry) {
  patchable_set48(T9, (long)entry);
  jr(T9);
}

void MacroAssembler::jmp(address entry, relocInfo::relocType rtype) {
  switch (rtype) {
    case relocInfo::runtime_call_type:
    case relocInfo::none:
      jmp(entry);
      break;
    default:
      {
      InstructionMark im(this);
      relocate(rtype);
      patchable_set48(T9, (long)entry);
      jr(T9);
      }
      break;
  }
}

void MacroAssembler::jmp_far(Label& L) {
  if (L.is_bound()) {
    address entry = target(L);
    assert(entry != NULL, "jmp most probably wrong");
    InstructionMark im(this);

    relocate(relocInfo::internal_word_type);
    patchable_set48(T9, (long)entry);
  } else {
    InstructionMark im(this);
    L.add_patch_at(code(), locator());

    relocate(relocInfo::internal_word_type);
    patchable_set48(T9, (long)pc());
  }

  jr(T9);
  delayed()->nop();
}
void MacroAssembler::mov_metadata(Address dst, Metadata* obj) {
  int oop_index;
  if (obj) {
    oop_index = oop_recorder()->find_index(obj);
  } else {
    oop_index = oop_recorder()->allocate_metadata_index(obj);
  }
  relocate(metadata_Relocation::spec(oop_index));
  patchable_set48(AT, (long)obj);
  sd(AT, dst);
}

void MacroAssembler::mov_metadata(Register dst, Metadata* obj) {
  int oop_index;
  if (obj) {
    oop_index = oop_recorder()->find_index(obj);
  } else {
    oop_index = oop_recorder()->allocate_metadata_index(obj);
  }
  relocate(metadata_Relocation::spec(oop_index));
  patchable_set48(dst, (long)obj);
}

void MacroAssembler::call(address entry) {
// c/c++ code assume T9 is entry point, so we just always move entry to t9
// maybe there is some more graceful method to handle this. FIXME
// For more info, see class NativeCall.
  patchable_set48(T9, (long)entry);
  jalr(T9);
}

void MacroAssembler::call(address entry, relocInfo::relocType rtype) {
  switch (rtype) {
    case relocInfo::runtime_call_type:
    case relocInfo::none:
      call(entry);
      break;
    default:
      {
  InstructionMark im(this);
  relocate(rtype);
  call(entry);
      }
      break;
  }
}

void MacroAssembler::call(address entry, RelocationHolder& rh)
{
  switch (rh.type()) {
    case relocInfo::runtime_call_type:
    case relocInfo::none:
      call(entry);
      break;
    default:
      {
  InstructionMark im(this);
  relocate(rh);
  call(entry);
      }
      break;
  }
}

void MacroAssembler::ic_call(address entry, jint method_index) {
  RelocationHolder rh = virtual_call_Relocation::spec(pc(), method_index);
  patchable_set48(IC_Klass, (long)Universe::non_oop_word());
  assert(entry != NULL, "call most probably wrong");
  InstructionMark im(this);
  trampoline_call(AddressLiteral(entry, rh));
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
  delayed()->nop();
  brk(17);
}

void MacroAssembler::warn(const char* msg) {
  pushad();
  li(A0, (long)msg);
  push(S2);
  move(AT, -(StackAlignmentInBytes));
  move(S2, SP);     // use S2 as a sender SP holder
  andr(SP, SP, AT); // align stack as required by ABI
  call(CAST_FROM_FN_PTR(address, MacroAssembler::debug), relocInfo::runtime_call_type);
  delayed()->nop();
  move(SP, S2);     // use S2 as a sender SP holder
  pop(S2);
  popad();
}

void MacroAssembler::increment(Register reg, int imm) {
  if (!imm) return;
  if (is_simm16(imm)) {
    daddiu(reg, reg, imm);
  } else {
    move(AT, imm);
    daddu(reg, reg, AT);
  }
}

void MacroAssembler::decrement(Register reg, int imm) {
  increment(reg, -imm);
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

  address before_call_pc;
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
  before_call_pc = (address)pc();
  set_last_Java_frame(java_thread, last_java_sp, FP, before_call_pc);

  // do the call
  move(A0, java_thread);
  call(entry_point, relocInfo::runtime_call_type);
  delayed()->nop();

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
    delayed()->nop();
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
    ld(AT, java_thread, in_bytes(Thread::pending_exception_offset()));
    beq(AT, R0, L);
    delayed()->nop();
    li(AT, before_call_pc);
    push(AT);
    jmp(StubRoutines::forward_exception_entry(), relocInfo::runtime_call_type);
    delayed()->nop();
    bind(L);
  }

  // get oop result if there is one and reset the value in the thread
  if (oop_result->is_valid()) {
    ld(oop_result, java_thread, in_bytes(JavaThread::vm_result_offset()));
    sd(R0, java_thread, in_bytes(JavaThread::vm_result_offset()));
    verify_oop(oop_result);
  }
}

void MacroAssembler::call_VM_helper(Register oop_result, address entry_point, int number_of_arguments, bool check_exceptions) {

  move(V0, SP);
  //we also reserve space for java_thread here
  move(AT, -(StackAlignmentInBytes));
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
    lw(AT, reg, 0);
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
//
//  In MIPS64, we don't use full 64-bit address space.
//  Only a small range is actually used.
//
//  Example:
//  $  cat /proc/13352/maps
//  120000000-120010000 r-xp 00000000 08:01 41077                            /mnt/openjdk6-mips-full/build/linux-mips64/j2sdk-image/bin/java
//  12001c000-120020000 rw-p 0000c000 08:01 41077                            /mnt/openjdk6-mips-full/build/linux-mips64/j2sdk-image/bin/java
//  120020000-1208dc000 rwxp 00000000 00:00 0                                [heap]
//  555d574000-555d598000 r-xp 00000000 08:01 2073768                        /lib/ld-2.12.so
//  555d598000-555d59c000 rw-p 00000000 00:00 0
//  ......
//  558b1f8000-558b23c000 rwxp 00000000 00:00 0
//  558b23c000-558b248000 ---p 00000000 00:00 0
//  558b248000-558b28c000 rwxp 00000000 00:00 0
//  ffff914000-ffff94c000 rwxp 00000000 00:00 0                              [stack]
//  ffffffc000-10000000000 r-xp 00000000 00:00 0                             [vdso]
//
//  All stacks are positioned at 0x55________.
//  Therefore, we can utilize the same algorithm used in 32-bit.
  // int index = ((uintptr_t)p >> PAGE_SHIFT) & ((1UL << (SP_BITLENGTH - PAGE_SHIFT)) - 1);
  // Thread* thread = _sp_map[index];
  Register tmp;

  if (thread == AT)
    tmp = T9;
  else
    tmp = AT;

  move(thread, SP);
  shr(thread, PAGE_SHIFT);

  push(tmp);
  li(tmp, ((1UL << (SP_BITLENGTH - PAGE_SHIFT)) - 1));
  andr(thread, thread, tmp);
  shl(thread, Address::times_ptr); // sizeof(Thread *)
  li48(tmp, (long)ThreadLocalStorage::sp_map_addr());
  addu(tmp, tmp, thread);
  ld_ptr(thread, tmp, 0);
  pop(tmp);
#else
  if (thread != V0) {
    push(V0);
  }
  pushad_except_v0();

  push(S5);
  move(S5, SP);
  move(AT, -StackAlignmentInBytes);
  andr(SP, SP, AT);
  call(CAST_FROM_FN_PTR(address, Thread::current));
  //MacroAssembler::call_VM_leaf_base(CAST_FROM_FN_PTR(address, Thread::current), 0);
  delayed()->nop();
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
  sd(R0, Address(thread, JavaThread::last_Java_sp_offset()));
  // must clear fp, so that compiled frames are not confused; it is
  // possible that we need it only for debugging
  if (clear_fp) {
    sd(R0, Address(thread, JavaThread::last_Java_fp_offset()));
  }

  // Always clear the pc because it could have been set by make_walkable()
  sd(R0, Address(thread, JavaThread::last_Java_pc_offset()));
}

// Write serialization page so VM thread can do a pseudo remote membar.
// We use the current thread pointer to calculate a thread specific
// offset to write to within the page. This minimizes bus traffic
// due to cache line collision.
void MacroAssembler::serialize_memory(Register thread, Register tmp) {
  int mask = os::vm_page_size() - sizeof(int);
  assert_different_registers(AT, tmp);
  assert(is_uimm(mask, 16), "Not a unsigned 16-bit");
  srl(AT, thread, os::get_serialize_page_shift_count());
  andi(AT, AT, mask);
  li(tmp, os::get_memory_serialize_page());
  addu(tmp, tmp, AT);
  sw(R0, tmp, 0);
}

void MacroAssembler::safepoint_poll(Label& slow_path, Register thread_reg) {
  if (SafepointMechanism::uses_thread_local_poll()) {
    ld(AT, thread_reg, in_bytes(Thread::polling_page_offset()));
    andi(AT, AT, SafepointMechanism::poll_bit());
    bne(AT, R0, slow_path);
    delayed()->nop();
  } else {
    li(AT, SafepointSynchronize::address_of_state());
    lw(AT, AT, 0);
    addiu(AT, AT, -SafepointSynchronize::_not_synchronized);
    bne(AT, R0, slow_path);
    delayed()->nop();
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
    ld(AT, thread_reg, in_bytes(Thread::polling_page_offset()));
    sync();
    andi(AT, AT, SafepointMechanism::poll_bit());
    bne(AT, R0, slow_path);
    delayed()->nop();
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
                                         address  last_java_pc) {
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

  // last_java_pc is optional
  if (last_java_pc != NULL) {
    relocate(relocInfo::internal_word_type);
    patchable_set48(AT, (long)last_java_pc);
    st_ptr(AT, java_thread, in_bytes(JavaThread::frame_anchor_offset() + JavaFrameAnchor::last_Java_pc_offset()));
  }
  st_ptr(last_java_sp, java_thread, in_bytes(JavaThread::last_Java_sp_offset()));
}

void MacroAssembler::set_last_Java_frame(Register last_java_sp,
                                         Register last_java_fp,
                                         address  last_java_pc) {
  // determine last_java_sp register
  if (!last_java_sp->is_valid()) {
    last_java_sp = SP;
  }

  Register thread = TREG;
#ifndef OPT_THREAD
  get_thread(thread);
#endif
  // last_java_fp is optional
  if (last_java_fp->is_valid()) {
    sd(last_java_fp, Address(thread, JavaThread::last_Java_fp_offset()));
  }

  // last_java_pc is optional
  if (last_java_pc != NULL) {
    relocate(relocInfo::internal_word_type);
    patchable_set48(AT, (long)last_java_pc);
    st_ptr(AT, thread, in_bytes(JavaThread::frame_anchor_offset() + JavaFrameAnchor::last_Java_pc_offset()));
  }

  sd(last_java_sp, Address(thread, JavaThread::last_Java_sp_offset()));
}

// Defines obj, preserves var_size_in_bytes, okay for t2 == var_size_in_bytes.
void MacroAssembler::tlab_allocate(Register obj, Register var_size_in_bytes, int con_size_in_bytes,
                                   Register t1, Register t2, Label& slow_case) {
  Unimplemented();
  //BarrierSetAssembler* bs = BarrierSet::barrier_set()->barrier_set_assembler();
  //bs->tlab_allocate(this, thread, obj, var_size_in_bytes, con_size_in_bytes, t1, t2, slow_case);
}

// Defines obj, preserves var_size_in_bytes
void MacroAssembler::eden_allocate(Register obj, Register var_size_in_bytes, int con_size_in_bytes,
                                   Register t1, Register t2, Label& slow_case) {
  Unimplemented();
  //assert_different_registers(obj, var_size_in_bytes, t1, AT);
  //BarrierSetAssembler* bs = BarrierSet::barrier_set()->barrier_set_assembler();
  //bs->eden_allocate(this, thread, obj, var_size_in_bytes, con_size_in_bytes, t1, slow_case);
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
    addu(AT, AT, var_size_in_bytes);
  } else {
    addiu(AT, AT, con_size_in_bytes);
  }
  st_ptr(AT, thread, in_bytes(JavaThread::allocated_bytes_offset()));
}

void MacroAssembler::li(Register rd, long imm) {
  if (imm <= max_jint && imm >= min_jint) {
    li32(rd, (int)imm);
  } else if (julong(imm) <= 0xFFFFFFFF) {
    assert_not_delayed();
    // lui sign-extends, so we can't use that.
    ori(rd, R0, julong(imm) >> 16);
    dsll(rd, rd, 16);
    ori(rd, rd, split_low(imm));
  } else if ((imm > 0) && is_simm16(imm >> 32)) {
    // A 48-bit address
    li48(rd, imm);
  } else {
    li64(rd, imm);
  }
}

void MacroAssembler::li32(Register reg, int imm) {
  if (is_simm16(imm)) {
    addiu(reg, R0, imm);
  } else {
    lui(reg, split_low(imm >> 16));
    if (split_low(imm))
      ori(reg, reg, split_low(imm));
  }
}

void MacroAssembler::set64(Register d, jlong value) {
  assert_not_delayed();

  int hi = (int)(value >> 32);
  int lo = (int)(value & ~0);

  if (value == lo) {  // 32-bit integer
    if (is_simm16(value)) {
      daddiu(d, R0, value);
    } else {
      lui(d, split_low(value >> 16));
      if (split_low(value)) {
        ori(d, d, split_low(value));
      }
    }
  } else if (hi == 0) {  // hardware zero-extends to upper 32
      ori(d, R0, julong(value) >> 16);
      dsll(d, d, 16);
      if (split_low(value)) {
        ori(d, d, split_low(value));
      }
  } else if ((value> 0) && is_simm16(value >> 32)) {  // li48
    // 4 insts
    li48(d, value);
  } else {  // li64
    // 6 insts
    li64(d, value);
  }
}


int MacroAssembler::insts_for_set64(jlong value) {
  int hi = (int)(value >> 32);
  int lo = (int)(value & ~0);

  int count = 0;

  if (value == lo) {  // 32-bit integer
    if (is_simm16(value)) {
      //daddiu(d, R0, value);
      count++;
    } else {
      //lui(d, split_low(value >> 16));
      count++;
      if (split_low(value)) {
        //ori(d, d, split_low(value));
        count++;
      }
    }
  } else if (hi == 0) {  // hardware zero-extends to upper 32
      //ori(d, R0, julong(value) >> 16);
      //dsll(d, d, 16);
      count += 2;
      if (split_low(value)) {
        //ori(d, d, split_low(value));
        count++;
      }
  } else if ((value> 0) && is_simm16(value >> 32)) {  // li48
    // 4 insts
    //li48(d, value);
    count += 4;
  } else {  // li64
    // 6 insts
    //li64(d, value);
    count += 6;
  }

  return count;
}

void MacroAssembler::patchable_set48(Register d, jlong value) {
  assert_not_delayed();

  int hi = (int)(value >> 32);
  int lo = (int)(value & ~0);

  int count = 0;

  if (value == lo) {  // 32-bit integer
    if (is_simm16(value)) {
      daddiu(d, R0, value);
      count += 1;
    } else {
      lui(d, split_low(value >> 16));
      count += 1;
      if (split_low(value)) {
        ori(d, d, split_low(value));
        count += 1;
      }
    }
  } else if (hi == 0) {  // hardware zero-extends to upper 32
      ori(d, R0, julong(value) >> 16);
      dsll(d, d, 16);
      count += 2;
      if (split_low(value)) {
        ori(d, d, split_low(value));
        count += 1;
      }
  } else if ((value> 0) && is_simm16(value >> 32)) {  // li48
    // 4 insts
    li48(d, value);
    count += 4;
  } else {  // li64
    tty->print_cr("value = 0x%lx", value);
    guarantee(false, "Not supported yet !");
  }

  while (count < 4) {
    nop();
    count++;
  }
}

void MacroAssembler::patchable_set32(Register d, jlong value) {
  assert_not_delayed();

  int hi = (int)(value >> 32);
  int lo = (int)(value & ~0);

  int count = 0;

  if (value == lo) {  // 32-bit integer
    if (is_simm16(value)) {
      daddiu(d, R0, value);
      count += 1;
    } else {
      lui(d, split_low(value >> 16));
      count += 1;
      if (split_low(value)) {
        ori(d, d, split_low(value));
        count += 1;
      }
    }
  } else if (hi == 0) {  // hardware zero-extends to upper 32
      ori(d, R0, julong(value) >> 16);
      dsll(d, d, 16);
      count += 2;
      if (split_low(value)) {
        ori(d, d, split_low(value));
        count += 1;
      }
  } else {
    tty->print_cr("value = 0x%lx", value);
    guarantee(false, "Not supported yet !");
  }

  while (count < 3) {
    nop();
    count++;
  }
}

void MacroAssembler::patchable_call32(Register d, jlong value) {
  assert_not_delayed();

  int hi = (int)(value >> 32);
  int lo = (int)(value & ~0);

  int count = 0;

  if (value == lo) {  // 32-bit integer
    if (is_simm16(value)) {
      daddiu(d, R0, value);
      count += 1;
    } else {
      lui(d, split_low(value >> 16));
      count += 1;
      if (split_low(value)) {
        ori(d, d, split_low(value));
        count += 1;
      }
    }
  } else {
    tty->print_cr("value = 0x%lx", value);
    guarantee(false, "Not supported yet !");
  }

  while (count < 2) {
    nop();
    count++;
  }
}

void MacroAssembler::set_narrow_klass(Register dst, Klass* k) {
  assert(UseCompressedClassPointers, "should only be used for compressed header");
  assert(oop_recorder() != NULL, "this assembler needs an OopRecorder");

  int klass_index = oop_recorder()->find_index(k);
  RelocationHolder rspec = metadata_Relocation::spec(klass_index);
  long narrowKlass = (long)Klass::encode_klass(k);

  relocate(rspec, Assembler::narrow_oop_operand);
  patchable_set48(dst, narrowKlass);
}


void MacroAssembler::set_narrow_oop(Register dst, jobject obj) {
  assert(UseCompressedOops, "should only be used for compressed header");
  assert(oop_recorder() != NULL, "this assembler needs an OopRecorder");

  int oop_index = oop_recorder()->find_index(obj);
  RelocationHolder rspec = oop_Relocation::spec(oop_index);

  relocate(rspec, Assembler::narrow_oop_operand);
  patchable_set48(dst, oop_index);
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

void MacroAssembler::li64(Register rd, long imm) {
  assert_not_delayed();
  lui(rd, split_low(imm >> 48));
  ori(rd, rd, split_low(imm >> 32));
  dsll(rd, rd, 16);
  ori(rd, rd, split_low(imm >> 16));
  dsll(rd, rd, 16);
  ori(rd, rd, split_low(imm));
}

void MacroAssembler::li48(Register rd, long imm) {
  assert_not_delayed();
  assert(is_simm16(imm >> 32), "Not a 48-bit address");
  lui(rd, imm >> 32);
  ori(rd, rd, split_low(imm >> 16));
  dsll(rd, rd, 16);
  ori(rd, rd, split_low(imm));
}

void MacroAssembler::verify_oop(Register reg, const char* s) {
  if (!VerifyOops) return;
  const char * b = NULL;
  stringStream ss;
  ss.print("verify_oop: %s: %s", reg->name(), s);
  b = code_string(ss.as_string());
  pushad();
  move(A1, reg);
  li(A0, (long)b);
  li(AT, (long)StubRoutines::verify_oop_subroutine_entry_address());
  ld(T9, AT, 0);
  jalr(T9);
  delayed()->nop();
  popad();
}


void MacroAssembler::verify_oop_addr(Address addr, const char* s) {
  if (!VerifyOops) {
    nop();
    return;
  }
  // Pass register number to verify_oop_subroutine
  const char * b = NULL;
  stringStream ss;
  ss.print("verify_oop_addr: %s",  s);
  b = code_string(ss.as_string());

  addiu(SP, SP, - 7 * wordSize);
  st_ptr(T0, SP, 6 * wordSize);
  st_ptr(T1, SP, 5 * wordSize);
  st_ptr(RA, SP, 4 * wordSize);
  st_ptr(A0, SP, 3 * wordSize);
  st_ptr(A1, SP, 2 * wordSize);
  st_ptr(AT, SP, 1 * wordSize);
  st_ptr(T9, SP, 0);

  // addr may contain sp so we will have to adjust it based on the
  // pushes that we just did.
  if (addr.uses(SP)) {
    lea(A1, addr);
    ld_ptr(A1, Address(A1, 7 * wordSize));
  } else {
    ld_ptr(A1, addr);
  }
  li(A0, (long)b);
  // call indirectly to solve generation ordering problem
  li(AT, (long)StubRoutines::verify_oop_subroutine_entry_address());
  ld_ptr(T9, AT, 0);
  jalr(T9);
  delayed()->nop();
  ld_ptr(T0, SP, 6* wordSize);
  ld_ptr(T1, SP, 5* wordSize);
  ld_ptr(RA, SP, 4* wordSize);
  ld_ptr(A0, SP, 3* wordSize);
  ld_ptr(A1, SP, 2* wordSize);
  ld_ptr(AT, SP, 1* wordSize);
  ld_ptr(T9, SP, 0* wordSize);
  addiu(SP, SP, 7 * wordSize);
}

// used registers :  T0, T1
void MacroAssembler::verify_oop_subroutine() {
  // RA: ra
  // A0: char* error message
  // A1: oop   object to verify

  Label exit, error;
  // increment counter
  li(T0, (long)StubRoutines::verify_oop_count_addr());
  lw(AT, T0, 0);
  daddiu(AT, AT, 1);
  sw(AT, T0, 0);

  // make sure object is 'reasonable'
  beq(A1, R0, exit);         // if obj is NULL it is ok
  delayed()->nop();

  // Check if the oop is in the right area of memory
  // const int oop_mask = Universe::verify_oop_mask();
  // const int oop_bits = Universe::verify_oop_bits();
  const uintptr_t oop_mask = Universe::verify_oop_mask();
  const uintptr_t oop_bits = Universe::verify_oop_bits();
  li(AT, oop_mask);
  andr(T0, A1, AT);
  li(AT, oop_bits);
  bne(T0, AT, error);
  delayed()->nop();

  // make sure klass is 'reasonable'
  // add for compressedoops
  reinit_heapbase();
  // add for compressedoops
  load_klass(T0, A1);
  beq(T0, R0, error);                        // if klass is NULL it is broken
  delayed()->nop();
  // return if everything seems ok
  bind(exit);

  jr(RA);
  delayed()->nop();

  // handle errors
  bind(error);
  pushad();
  call(CAST_FROM_FN_PTR(address, MacroAssembler::debug), relocInfo::runtime_call_type);
  delayed()->nop();
  popad();
  jr(RA);
  delayed()->nop();
}

void MacroAssembler::verify_tlab(Register t1, Register t2) {
#ifdef ASSERT
  assert_different_registers(t1, t2, AT);
  if (UseTLAB && VerifyOops) {
    Label next, ok;

    get_thread(t1);

    ld_ptr(t2, t1, in_bytes(JavaThread::tlab_top_offset()));
    ld_ptr(AT, t1, in_bytes(JavaThread::tlab_start_offset()));
    sltu(AT, t2, AT);
    beq(AT, R0, next);
    delayed()->nop();

    stop("assert(top >= start)");

    bind(next);
    ld_ptr(AT, t1, in_bytes(JavaThread::tlab_end_offset()));
    sltu(AT, AT, t2);
    beq(AT, R0, ok);
    delayed()->nop();

    stop("assert(top <= end)");

    bind(ok);

  }
#endif
}
RegisterOrConstant MacroAssembler::delayed_value_impl(intptr_t* delayed_value_addr,
                                                      Register tmp,
                                                      int offset) {
  intptr_t value = *delayed_value_addr;
  if (value != 0)
  return RegisterOrConstant(value + offset);
  Unimplemented();
  //AddressLiteral a(delayed_value_addr);
  // load indirectly to solve generation ordering problem
  //movptr(tmp, ExternalAddress((address) delayed_value_addr));
  //ld(tmp, a);
  if (offset != 0)
    daddiu(tmp,tmp, offset);

  return RegisterOrConstant(tmp);
}

void MacroAssembler::hswap(Register reg) {
  //short
  //andi(reg, reg, 0xffff);
  srl(AT, reg, 8);
  sll(reg, reg, 24);
  sra(reg, reg, 16);
  orr(reg, reg, AT);
}

void MacroAssembler::huswap(Register reg) {
  dsrl(AT, reg, 8);
  dsll(reg, reg, 24);
  dsrl(reg, reg, 16);
  orr(reg, reg, AT);
  andi(reg, reg, 0xffff);
}

// something funny to do this will only one more register AT
// 32 bits
void MacroAssembler::swap(Register reg) {
  srl(AT, reg, 8);
  sll(reg, reg, 24);
  orr(reg, reg, AT);
  //reg : 4 1 2 3
  srl(AT, AT, 16);
  xorr(AT, AT, reg);
  andi(AT, AT, 0xff);
  //AT : 0 0 0 1^3);
  xorr(reg, reg, AT);
  //reg : 4 1 2 1
  sll(AT, AT, 16);
  xorr(reg, reg, AT);
  //reg : 4 3 2 1
}

void MacroAssembler::cmpxchg(Address addr, Register oldval, Register newval,
                             Register resflag, bool retold, bool barrier) {
  assert(oldval != resflag, "oldval != resflag");
  assert(newval != resflag, "newval != resflag");
  Label again, succ, fail;
  bind(again);
  lld(resflag, addr);
  bne(resflag, oldval, fail);
  delayed()->nop();
  move(resflag, newval);
  scd(resflag, addr);
  beq(resflag, R0, again);
  delayed()->nop();
  b(succ);
  delayed()->nop();
  bind(fail);
  if (barrier)
    sync();
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
  lld(tmp, addr);
  bne(tmp, oldval, neq);
  delayed()->nop();
  move(tmp, newval);
  scd(tmp, addr);
  beq(tmp, R0, again);
  delayed()->nop();
  b(succ);
  delayed()->nop();

  bind(neq);
  if (barrier)
    sync();
  if (retold && oldval != R0)
    move(oldval, tmp);
  if (fail) {
    b(*fail);
    delayed()->nop();
  }
}


void MacroAssembler::cmpxchg32(Address addr, Register oldval, Register newval,
                               Register resflag, bool sign, bool retold, bool barrier) {
  assert(oldval != resflag, "oldval != resflag");
  assert(newval != resflag, "newval != resflag");
  Label again, succ, fail;
  bind(again);
  ll(resflag, addr);
  if (!sign)
    dinsu(resflag, R0, 32, 32);
  bne(resflag, oldval, fail);
  delayed()->nop();

  move(resflag, newval);
  sc(resflag, addr);
  beq(resflag, R0, again);
  delayed()->nop();
  b(succ);
  delayed()->nop();

  bind(fail);
  if (barrier)
    sync();
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
  ll(tmp, addr);
  if (!sign)
    dinsu(tmp, R0, 32, 32);
  bne(tmp, oldval, neq);
  delayed()->nop();
  move(tmp, newval);
  sc(tmp, addr);
  beq(tmp, R0, again);
  delayed()->nop();
  b(succ);
  delayed()->nop();

  bind(neq);
  if (barrier)
    sync();
  if (retold && oldval != R0)
    move(oldval, tmp);
  if (fail) {
    b(*fail);
    delayed()->nop();
  }
}

void MacroAssembler::cmpxchg8(Register x_regLo, Register x_regHi, Address dest, Register c_regLo, Register c_regHi) {
  Label done, again, nequal;

  Register x_reg = x_regLo;
  dsll32(x_regHi, x_regHi, 0);
  dsll32(x_regLo, x_regLo, 0);
  dsrl32(x_regLo, x_regLo, 0);
  orr(x_reg, x_regLo, x_regHi);

  Register c_reg = c_regLo;
  dsll32(c_regHi, c_regHi, 0);
  dsll32(c_regLo, c_regLo, 0);
  dsrl32(c_regLo, c_regLo, 0);
  orr(c_reg, c_regLo, c_regHi);

  bind(again);

  if (UseSyncLevel >= 10000 || UseSyncLevel == 1000 || UseSyncLevel == 4000) sync();
  lld(AT, dest);
  bne(AT, c_reg, nequal);
  delayed()->nop();

  //move(AT, x_reg);
  daddu(AT, x_reg, R0);
  scd(AT, dest);
  beq(AT, R0, again);
  delayed()->nop();
  b(done);
  delayed()->nop();

  // not xchged
  bind(nequal);
  sync();
  //move(c_reg, AT);
  //move(AT, R0);
  daddu(c_reg, AT, R0);
  daddu(AT, R0, R0);
  bind(done);
}

// be sure the three register is different
void MacroAssembler::rem_s(FloatRegister fd, FloatRegister fs, FloatRegister ft, FloatRegister tmp) {
  assert_different_registers(tmp, fs, ft);
  div_s(tmp, fs, ft);
  trunc_l_s(tmp, tmp);
  cvt_s_l(tmp, tmp);
  mul_s(tmp, tmp, ft);
  sub_s(fd, fs, tmp);
}

// be sure the three register is different
void MacroAssembler::rem_d(FloatRegister fd, FloatRegister fs, FloatRegister ft, FloatRegister tmp) {
  assert_different_registers(tmp, fs, ft);
  div_d(tmp, fs, ft);
  trunc_l_d(tmp, tmp);
  cvt_d_l(tmp, tmp);
  mul_d(tmp, tmp, ft);
  sub_d(fd, fs, tmp);
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
    move(AT, 0x0);
    return;
  } else
    if (EmitSync & 2) {
      Label DONE_LABEL ;
      if (UseBiasedLocking) {
        // Note: tmpReg maps to the swap_reg argument and scrReg to the tmp_reg argument.
        biased_locking_enter(boxReg, objReg, tmpReg, scrReg, false, DONE_LABEL, NULL);
      }

      ld(tmpReg, Address(objReg, 0)) ;          // fetch markword
      ori(tmpReg, tmpReg, 0x1);
      sd(tmpReg, Address(boxReg, 0));           // Anticipate successful CAS

      cmpxchg(Address(objReg, 0), tmpReg, boxReg, scrReg, true, false, DONE_LABEL); // Updates tmpReg
      delayed()->nop();

      // Recursive locking
      dsubu(tmpReg, tmpReg, SP);
      li(AT, (7 - os::vm_page_size() ));
      andr(tmpReg, tmpReg, AT);
      sd(tmpReg, Address(boxReg, 0));
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
        delayed()->nop();
        bind(succ);
        b(DONE);
        delayed()->ori(resReg, R0, 1);
        bind(fail);
      }

      ld(tmpReg, Address(objReg, 0)); //Fetch the markword of the object.
      andi(AT, tmpReg, markOopDesc::monitor_value);
      bne(AT, R0, IsInflated); // inflated vs stack-locked|neutral|bias
      delayed()->nop();

      // Attempt stack-locking ...
      ori(tmpReg, tmpReg, markOopDesc::unlocked_value);
      sd(tmpReg, Address(boxReg, 0)); // Anticipate successful CAS

      if (PrintBiasedLockingStatistics) {
        Label SUCC, FAIL;
        cmpxchg(Address(objReg, 0), tmpReg, boxReg, scrReg, true, false, SUCC, &FAIL); // Updates tmpReg
        bind(SUCC);
        atomic_inc32((address)BiasedLocking::fast_path_entry_count_addr(), 1, AT, scrReg);
        b(DONE);
        delayed()->ori(resReg, R0, 1);
        bind(FAIL);
      } else {
        // If cmpxchg is succ, then scrReg = 1
        cmpxchg(Address(objReg, 0), tmpReg, boxReg, scrReg, true, false, DONE_SET); // Updates tmpReg
      }

      // Recursive locking
      // The object is stack-locked: markword contains stack pointer to BasicLock.
      // Locked by current thread if difference with current SP is less than one page.
      dsubu(tmpReg, tmpReg, SP);
      li(AT, 7 - os::vm_page_size());
      andr(tmpReg, tmpReg, AT);
      sd(tmpReg, Address(boxReg, 0));

      if (PrintBiasedLockingStatistics) {
        Label L;
        // tmpReg == 0 => BiasedLocking::_fast_path_entry_count++
        bne(tmpReg, R0, L);
        delayed()->nop();
        atomic_inc32((address)BiasedLocking::fast_path_entry_count_addr(), 1, AT, scrReg);
        bind(L);
      }
      b(DONE);
      delayed()->sltiu(resReg, tmpReg, 1); // resReg = (tmpReg == 0) ? 1 : 0

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
      sd(AT, Address(boxReg, 0));

      ld(AT, Address(tmpReg, ObjectMonitor::owner_offset_in_bytes() - 2));
      // if (m->owner != 0) => AT = 0, goto slow path.
      bne(AT, R0, DONE_SET);
      delayed()->ori(scrReg, R0, 0);

#ifndef OPT_THREAD
      get_thread(TREG);
#endif
      // It's inflated and appears unlocked
      cmpxchg(Address(tmpReg, ObjectMonitor::owner_offset_in_bytes() - 2), R0, TREG, scrReg, false, false) ;
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
    move(AT, 0x0);
    return;
  } else
    if (EmitSync & 8) {
      Label DONE_LABEL ;
      if (UseBiasedLocking) {
        biased_locking_exit(objReg, tmpReg, DONE_LABEL);
      }
      // classic stack-locking code ...
      ld(tmpReg, Address(boxReg, 0)) ;
      beq(tmpReg, R0, DONE_LABEL) ;
      move(AT, 0x1);  // delay slot

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
        delayed()->nop();
        bind(succ);
        b(DONE);
        delayed()->ori(resReg, R0, 1);
        bind(fail);
      }

      ld(tmpReg, Address(boxReg, 0)); // Examine the displaced header
      beq(tmpReg, R0, DONE_SET); // 0 indicates recursive stack-lock
      delayed()->sltiu(AT, tmpReg, 1);

      ld(tmpReg, Address(objReg, 0)); // Examine the object's markword
      andi(AT, tmpReg, markOopDesc::monitor_value);
      beq(AT, R0, Stacked); // Inflated?
      delayed()->nop();

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
      ld(scrReg, Address(tmpReg, ObjectMonitor::owner_offset_in_bytes() - 2)) ;
      xorr(scrReg, scrReg, TREG);

      ld(AT, Address(tmpReg, ObjectMonitor::recursions_offset_in_bytes() - 2)) ;
      orr(scrReg, scrReg, AT);

      bne(scrReg, R0, DONE_SET);
      delayed()->ori(AT, R0, 0);

      ld(scrReg, Address(tmpReg, ObjectMonitor::cxq_offset_in_bytes() - 2));
      ld(AT, Address(tmpReg, ObjectMonitor::EntryList_offset_in_bytes() - 2));
      orr(scrReg, scrReg, AT);

      bne(scrReg, R0, DONE_SET);
      delayed()->ori(AT, R0, 0);

      sync();
      sd(R0, Address(tmpReg, ObjectMonitor::owner_offset_in_bytes() - 2));
      b(DONE);
      delayed()->ori(resReg, R0, 1);

      bind(Stacked);
      ld(tmpReg, Address(boxReg, 0));
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

Register caller_saved_registers[] = {AT, V0, V1, A0, A1, A2, A3, A4, A5, A6, A7, T0, T1, T2, T3, T8, T9, GP, RA, FP};
Register caller_saved_registers_except_v0[] = {AT, V1, A0, A1, A2, A3, A4, A5, A6, A7, T0, T1, T2, T3, T8, T9, GP, RA, FP};

//In MIPS64, F0~23 are all caller-saved registers
FloatRegister caller_saved_fpu_registers[] = {F0, F12, F13};

// We preserve all caller-saved register
void  MacroAssembler::pushad(){
  int i;

  // Fixed-point registers
  int len = sizeof(caller_saved_registers) / sizeof(caller_saved_registers[0]);
  daddiu(SP, SP, -1 * len * wordSize);
  for (i = 0; i < len; i++)
  {
    sd(caller_saved_registers[i], SP, (len - i - 1) * wordSize);
  }

  // Floating-point registers
  len = sizeof(caller_saved_fpu_registers) / sizeof(caller_saved_fpu_registers[0]);
  daddiu(SP, SP, -1 * len * wordSize);
  for (i = 0; i < len; i++)
  {
    sdc1(caller_saved_fpu_registers[i], SP, (len - i - 1) * wordSize);
  }
};

void  MacroAssembler::popad(){
  int i;

  // Floating-point registers
  int len = sizeof(caller_saved_fpu_registers) / sizeof(caller_saved_fpu_registers[0]);
  for (i = 0; i < len; i++)
  {
    ldc1(caller_saved_fpu_registers[i], SP, (len - i - 1) * wordSize);
  }
  daddiu(SP, SP, len * wordSize);

  // Fixed-point registers
  len = sizeof(caller_saved_registers) / sizeof(caller_saved_registers[0]);
  for (i = 0; i < len; i++)
  {
    ld(caller_saved_registers[i], SP, (len - i - 1) * wordSize);
  }
  daddiu(SP, SP, len * wordSize);
};

// We preserve all caller-saved register except V0
void MacroAssembler::pushad_except_v0() {
  int i;

  // Fixed-point registers
  int len = sizeof(caller_saved_registers_except_v0) / sizeof(caller_saved_registers_except_v0[0]);
  daddiu(SP, SP, -1 * len * wordSize);
  for (i = 0; i < len; i++) {
    sd(caller_saved_registers_except_v0[i], SP, (len - i - 1) * wordSize);
  }

  // Floating-point registers
  len = sizeof(caller_saved_fpu_registers) / sizeof(caller_saved_fpu_registers[0]);
  daddiu(SP, SP, -1 * len * wordSize);
  for (i = 0; i < len; i++) {
    sdc1(caller_saved_fpu_registers[i], SP, (len - i - 1) * wordSize);
  }
}

void MacroAssembler::popad_except_v0() {
  int i;

  // Floating-point registers
  int len = sizeof(caller_saved_fpu_registers) / sizeof(caller_saved_fpu_registers[0]);
  for (i = 0; i < len; i++) {
    ldc1(caller_saved_fpu_registers[i], SP, (len - i - 1) * wordSize);
  }
  daddiu(SP, SP, len * wordSize);

  // Fixed-point registers
  len = sizeof(caller_saved_registers_except_v0) / sizeof(caller_saved_registers_except_v0[0]);
  for (i = 0; i < len; i++) {
    ld(caller_saved_registers_except_v0[i], SP, (len - i - 1) * wordSize);
  }
  daddiu(SP, SP, len * wordSize);
}

void MacroAssembler::push2(Register reg1, Register reg2) {
  daddiu(SP, SP, -16);
  sd(reg1, SP, 8);
  sd(reg2, SP, 0);
}

void MacroAssembler::pop2(Register reg1, Register reg2) {
  ld(reg1, SP, 8);
  ld(reg2, SP, 0);
  daddiu(SP, SP, 16);
}

// for UseCompressedOops Option
void MacroAssembler::load_klass(Register dst, Register src) {
  if(UseCompressedClassPointers){
    lwu(dst, Address(src, oopDesc::klass_offset_in_bytes()));
    decode_klass_not_null(dst);
  } else
  ld(dst, src, oopDesc::klass_offset_in_bytes());
}

void MacroAssembler::store_klass(Register dst, Register src) {
  if(UseCompressedClassPointers){
    encode_klass_not_null(src);
    sw(src, dst, oopDesc::klass_offset_in_bytes());
  } else {
    sd(src, dst, oopDesc::klass_offset_in_bytes());
  }
}

void MacroAssembler::load_prototype_header(Register dst, Register src) {
  load_klass(dst, src);
  ld(dst, Address(dst, Klass::prototype_header_offset()));
}

void MacroAssembler::store_klass_gap(Register dst, Register src) {
  if (UseCompressedClassPointers) {
    sw(src, dst, oopDesc::klass_gap_offset_in_bytes());
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
      assert (LogMinObjAlignmentInBytes == Universe::narrow_oop_shift(), "decode alg wrong");
      shr(r, LogMinObjAlignmentInBytes);
    }
    return;
  }

  movz(r, S5_heapbase, r);
  dsubu(r, r, S5_heapbase);
  if (Universe::narrow_oop_shift() != 0) {
    assert (LogMinObjAlignmentInBytes == Universe::narrow_oop_shift(), "decode alg wrong");
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
      assert (LogMinObjAlignmentInBytes == Universe::narrow_oop_shift(), "decode alg wrong");
      dsrl(dst, src, LogMinObjAlignmentInBytes);
    } else {
      if (dst != src) move(dst, src);
    }
  } else {
    if (dst == src) {
      movz(dst, S5_heapbase, dst);
      dsubu(dst, dst, S5_heapbase);
      if (Universe::narrow_oop_shift() != 0) {
        assert (LogMinObjAlignmentInBytes == Universe::narrow_oop_shift(), "decode alg wrong");
        shr(dst, LogMinObjAlignmentInBytes);
      }
    } else {
      dsubu(dst, src, S5_heapbase);
      if (Universe::narrow_oop_shift() != 0) {
        assert (LogMinObjAlignmentInBytes == Universe::narrow_oop_shift(), "decode alg wrong");
        shr(dst, LogMinObjAlignmentInBytes);
      }
      movz(dst, R0, src);
    }
  }
}

void MacroAssembler::encode_heap_oop_not_null(Register r) {
  assert (UseCompressedOops, "should be compressed");
#ifdef ASSERT
  if (CheckCompressedOops) {
    Label ok;
    bne(r, R0, ok);
    delayed()->nop();
    stop("null oop passed to encode_heap_oop_not_null");
    bind(ok);
  }
#endif
  verify_oop(r, "broken oop in encode_heap_oop_not_null");
  if (Universe::narrow_oop_base() != NULL) {
    dsubu(r, r, S5_heapbase);
  }
  if (Universe::narrow_oop_shift() != 0) {
    assert (LogMinObjAlignmentInBytes == Universe::narrow_oop_shift(), "decode alg wrong");
    shr(r, LogMinObjAlignmentInBytes);
  }

}

void MacroAssembler::encode_heap_oop_not_null(Register dst, Register src) {
  assert (UseCompressedOops, "should be compressed");
#ifdef ASSERT
  if (CheckCompressedOops) {
    Label ok;
    bne(src, R0, ok);
    delayed()->nop();
    stop("null oop passed to encode_heap_oop_not_null2");
    bind(ok);
  }
#endif
  verify_oop(src, "broken oop in encode_heap_oop_not_null2");

  if (Universe::narrow_oop_base() != NULL) {
    dsubu(dst, src, S5_heapbase);
    if (Universe::narrow_oop_shift() != 0) {
      assert (LogMinObjAlignmentInBytes == Universe::narrow_oop_shift(), "decode alg wrong");
      shr(dst, LogMinObjAlignmentInBytes);
    }
  } else {
    if (Universe::narrow_oop_shift() != 0) {
      assert (LogMinObjAlignmentInBytes == Universe::narrow_oop_shift(), "decode alg wrong");
      dsrl(dst, src, LogMinObjAlignmentInBytes);
    } else {
      if (dst != src) move(dst, src);
    }
  }
}

void  MacroAssembler::decode_heap_oop(Register r) {
#ifdef ASSERT
  verify_heapbase("MacroAssembler::decode_heap_oop corrupted?");
#endif
  if (Universe::narrow_oop_base() == NULL) {
    if (Universe::narrow_oop_shift() != 0) {
      assert (LogMinObjAlignmentInBytes == Universe::narrow_oop_shift(), "decode alg wrong");
      shl(r, LogMinObjAlignmentInBytes);
    }
  } else {
    move(AT, r);
    if (Universe::narrow_oop_shift() != 0) {
      assert (LogMinObjAlignmentInBytes == Universe::narrow_oop_shift(), "decode alg wrong");
      shl(r, LogMinObjAlignmentInBytes);
    }
    daddu(r, r, S5_heapbase);
    movz(r, R0, AT);
  }
  verify_oop(r, "broken oop in decode_heap_oop");
}

void  MacroAssembler::decode_heap_oop(Register dst, Register src) {
#ifdef ASSERT
  verify_heapbase("MacroAssembler::decode_heap_oop corrupted?");
#endif
  if (Universe::narrow_oop_base() == NULL) {
    if (Universe::narrow_oop_shift() != 0) {
      assert (LogMinObjAlignmentInBytes == Universe::narrow_oop_shift(), "decode alg wrong");
      if (dst != src) nop(); // DON'T DELETE THIS GUY.
      dsll(dst, src, LogMinObjAlignmentInBytes);
    } else {
      if (dst != src) move(dst, src);
    }
  } else {
    if (dst == src) {
      move(AT, dst);
      if (Universe::narrow_oop_shift() != 0) {
        assert (LogMinObjAlignmentInBytes == Universe::narrow_oop_shift(), "decode alg wrong");
        shl(dst, LogMinObjAlignmentInBytes);
      }
      daddu(dst, dst, S5_heapbase);
      movz(dst, R0, AT);
    } else {
      if (Universe::narrow_oop_shift() != 0) {
        assert (LogMinObjAlignmentInBytes == Universe::narrow_oop_shift(), "decode alg wrong");
        dsll(dst, src, LogMinObjAlignmentInBytes);
        daddu(dst, dst, S5_heapbase);
      } else {
        daddu(dst, src, S5_heapbase);
      }
      movz(dst, R0, src);
    }
  }
  verify_oop(dst, "broken oop in decode_heap_oop");
}

void  MacroAssembler::decode_heap_oop_not_null(Register r) {
  // Note: it will change flags
  assert (UseCompressedOops, "should only be used for compressed headers");
  assert (Universe::heap() != NULL, "java heap should be initialized");
  // Cannot assert, unverified entry point counts instructions (see .ad file)
  // vtableStubs also counts instructions in pd_code_size_limit.
  // Also do not verify_oop as this is called by verify_oop.
  if (Universe::narrow_oop_shift() != 0) {
    assert(LogMinObjAlignmentInBytes == Universe::narrow_oop_shift(), "decode alg wrong");
    shl(r, LogMinObjAlignmentInBytes);
    if (Universe::narrow_oop_base() != NULL) {
      daddu(r, r, S5_heapbase);
    }
  } else {
    assert (Universe::narrow_oop_base() == NULL, "sanity");
  }
}

void  MacroAssembler::decode_heap_oop_not_null(Register dst, Register src) {
  assert (UseCompressedOops, "should only be used for compressed headers");
  assert (Universe::heap() != NULL, "java heap should be initialized");

  // Cannot assert, unverified entry point counts instructions (see .ad file)
  // vtableStubs also counts instructions in pd_code_size_limit.
  // Also do not verify_oop as this is called by verify_oop.
  //lea(dst, Address(S5_heapbase, src, Address::times_8, 0));
  if (Universe::narrow_oop_shift() != 0) {
    assert(LogMinObjAlignmentInBytes == Universe::narrow_oop_shift(), "decode alg wrong");
    if (LogMinObjAlignmentInBytes == Address::times_8) {
      dsll(dst, src, LogMinObjAlignmentInBytes);
      daddu(dst, dst, S5_heapbase);
    } else {
      dsll(dst, src, LogMinObjAlignmentInBytes);
      if (Universe::narrow_oop_base() != NULL) {
        daddu(dst, dst, S5_heapbase);
      }
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
    assert(r != AT, "Encoding a klass in AT");
    set64(AT, (int64_t)Universe::narrow_klass_base());
    dsubu(r, r, AT);
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
      set64(dst, (int64_t)Universe::narrow_klass_base());
      dsubu(dst, src, dst);
      if (Universe::narrow_klass_shift() != 0) {
        assert (LogKlassAlignmentInBytes == Universe::narrow_klass_shift(), "decode alg wrong");
        shr(dst, LogKlassAlignmentInBytes);
      }
    } else {
      if (Universe::narrow_klass_shift() != 0) {
        assert (LogKlassAlignmentInBytes == Universe::narrow_klass_shift(), "decode alg wrong");
        dsrl(dst, src, LogKlassAlignmentInBytes);
      } else {
        move(dst, src);
      }
    }
  }
}

// Function instr_size_for_decode_klass_not_null() counts the instructions
// generated by decode_klass_not_null(register r) and reinit_heapbase(),
// when (Universe::heap() != NULL).  Hence, if the instructions they
// generate change, then this method needs to be updated.
int MacroAssembler::instr_size_for_decode_klass_not_null() {
  assert (UseCompressedClassPointers, "only for compressed klass ptrs");
  if (Universe::narrow_klass_base() != NULL) {
    // mov64 + addq + shlq? + mov64  (for reinit_heapbase()).
    return (Universe::narrow_klass_shift() == 0 ? 4 * 9 : 4 * 10);
  } else {
    // longest load decode klass function, mov64, leaq
    return (Universe::narrow_klass_shift() == 0 ? 4 * 0 : 4 * 1);
  }
}

void  MacroAssembler::decode_klass_not_null(Register r) {
  assert (UseCompressedClassPointers, "should only be used for compressed headers");
  assert(r != AT, "Decoding a klass in AT");
  // Cannot assert, unverified entry point counts instructions (see .ad file)
  // vtableStubs also counts instructions in pd_code_size_limit.
  // Also do not verify_oop as this is called by verify_oop.
  if (Universe::narrow_klass_shift() != 0) {
    assert(LogKlassAlignmentInBytes == Universe::narrow_klass_shift(), "decode alg wrong");
    shl(r, LogKlassAlignmentInBytes);
  }
  if (Universe::narrow_klass_base() != NULL) {
    set64(AT, (int64_t)Universe::narrow_klass_base());
    daddu(r, r, AT);
    //Not neccessary for MIPS at all.
    //reinit_heapbase();
  }
}

void  MacroAssembler::decode_klass_not_null(Register dst, Register src) {
  assert (UseCompressedClassPointers, "should only be used for compressed headers");

  if (dst == src) {
    decode_klass_not_null(dst);
  } else {
    // Cannot assert, unverified entry point counts instructions (see .ad file)
    // vtableStubs also counts instructions in pd_code_size_limit.
    // Also do not verify_oop as this is called by verify_oop.
    set64(dst, (int64_t)Universe::narrow_klass_base());
    if (Universe::narrow_klass_shift() != 0) {
      assert(LogKlassAlignmentInBytes == Universe::narrow_klass_shift(), "decode alg wrong");
      assert(LogKlassAlignmentInBytes == Address::times_8, "klass not aligned on 64bits?");
      dsll(AT, src, Address::times_8);
      daddu(dst, dst, AT);
    } else {
      daddu(dst, src, dst);
    }
  }
}

void MacroAssembler::incrementl(Register reg, int value) {
  if (value == min_jint) {
     move(AT, value);
     addu32(reg, reg, AT);
     return;
  }
  if (value <  0) { decrementl(reg, -value); return; }
  if (value == 0) {                        ; return; }

  move(AT, value);
  addu32(reg, reg, AT);
}

void MacroAssembler::decrementl(Register reg, int value) {
  if (value == min_jint) {
     move(AT, value);
     subu32(reg, reg, AT);
     return;
  }
  if (value <  0) { incrementl(reg, -value); return; }
  if (value == 0) {                        ; return; }

  move(AT, value);
  subu32(reg, reg, AT);
}

void MacroAssembler::reinit_heapbase() {
  if (UseCompressedOops || UseCompressedClassPointers) {
    if (Universe::heap() != NULL) {
      if (Universe::narrow_oop_base() == NULL) {
        move(S5_heapbase, R0);
      } else {
        set64(S5_heapbase, (int64_t)Universe::narrow_ptrs_base());
      }
    } else {
      set64(S5_heapbase, (intptr_t)Universe::narrow_ptrs_base_addr());
      ld(S5_heapbase, S5_heapbase, 0);
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
  delayed()->nop();
  // Check the supertype display:
  if (must_load_sco) {
    lwu(temp_reg, super_klass, sco_offset);
    super_check_offset = RegisterOrConstant(temp_reg);
  }
  daddu(AT, sub_klass, super_check_offset.register_or_noreg());
  ld(AT, AT, super_check_offset.constant_or_zero());

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
    delayed()->nop();
    addiu(AT, super_check_offset.as_register(), -sc_offset);
    if (L_failure == &L_fallthrough) {
      beq(AT, R0, *L_slow_path);
      delayed()->nop();
    } else {
      bne_far(AT, R0, *L_failure);
      delayed()->nop();
      b(*L_slow_path);
      delayed()->nop();
    }
  } else if (super_check_offset.as_constant() == sc_offset) {
    // Need a slow path; fast failure is impossible.
    if (L_slow_path == &L_fallthrough) {
      beq(super_klass, AT, *L_success);
      delayed()->nop();
    } else {
      bne(super_klass, AT, *L_slow_path);
      delayed()->nop();
      b(*L_success);
      delayed()->nop();
    }
  } else {
    // No slow path; it's a fast decision.
    if (L_failure == &L_fallthrough) {
      beq(super_klass, AT, *L_success);
      delayed()->nop();
    } else {
      bne_far(super_klass, AT, *L_failure);
      delayed()->nop();
      b(*L_success);
      delayed()->nop();
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
  ld(temp_reg, secondary_supers_addr);
  // Load the array length.
  lw(temp2_reg, Address(temp_reg, Array<Klass*>::length_offset_in_bytes()));
  // Skip to start of data.
  daddiu(temp_reg, temp_reg, Array<Klass*>::base_offset_in_bytes());

  // OpenJDK8 never compresses klass pointers in secondary-super array.
  Label Loop, subtype;
  bind(Loop);
  beq(temp2_reg, R0, *L_failure);
  delayed()->nop();
  ld(AT, temp_reg, 0);
  beq(AT, super_klass, subtype);
  delayed()->daddiu(temp_reg, temp_reg, 1 * wordSize);
  b(Loop);
  delayed()->daddiu(temp2_reg, temp2_reg, -1);

  bind(subtype);
  sd(super_klass, super_cache_addr);
  if (L_success != &L_fallthrough) {
    b(*L_success);
    delayed()->nop();
  }

  // Success.  Cache the super we found and proceed in triumph.
#undef IS_A_TEMP

  bind(L_fallthrough);
}

void MacroAssembler::get_vm_result(Register oop_result, Register java_thread) {
  ld(oop_result, Address(java_thread, JavaThread::vm_result_offset()));
  sd(R0, Address(java_thread, JavaThread::vm_result_offset()));
  verify_oop(oop_result, "broken oop in call_VM_base");
}

void MacroAssembler::get_vm_result_2(Register metadata_result, Register java_thread) {
  ld(metadata_result, Address(java_thread, JavaThread::vm_result_2_offset()));
  sd(R0, Address(java_thread, JavaThread::vm_result_2_offset()));
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
  dsll(scale_reg, scale_reg, scale_factor);
  daddu(scale_reg, SP, scale_reg);
  return Address(scale_reg, offset);
  }
}

SkipIfEqual::~SkipIfEqual() {
  _masm->bind(_label);
}

void MacroAssembler::load_sized_value(Register dst, Address src, size_t size_in_bytes, bool is_signed, Register dst2) {
  switch (size_in_bytes) {
  case  8:  ld(dst, src); break;
  case  4:  lw(dst, src); break;
  case  2:  is_signed ? lh(dst, src) : lhu(dst, src); break;
  case  1:  is_signed ? lb( dst, src) : lbu( dst, src); break;
  default:  ShouldNotReachHere();
  }
}

void MacroAssembler::store_sized_value(Address dst, Register src, size_t size_in_bytes, Register src2) {
  switch (size_in_bytes) {
  case  8:  sd(src, dst); break;
  case  4:  sw(src, dst); break;
  case  2:  sh(src, dst); break;
  case  1:  sb(src, dst); break;
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

  lw(scan_temp, Address(recv_klass, Klass::vtable_length_offset()));

  // %%% Could store the aligned, prescaled offset in the klassoop.
  dsll(scan_temp, scan_temp, times_vte_scale);
  daddu(scan_temp, recv_klass, scan_temp);
  daddiu(scan_temp, scan_temp, vtable_base);
  if (HeapWordsPerLong > 1) {
    // Round up to align_object_offset boundary
    // see code for InstanceKlass::start_of_itable!
    round_to(scan_temp, BytesPerLong);
  }

  if (return_method) {
    // Adjust recv_klass by scaled itable_index, so we can free itable_index.
    assert(itableMethodEntry::size() * wordSize == wordSize, "adjust the scaling in the code below");
    if (itable_index.is_constant()) {
      set64(AT, (int)itable_index.is_constant());
      dsll(AT, AT, (int)Address::times_ptr);
    } else {
      dsll(AT, itable_index.as_register(), (int)Address::times_ptr);
    }
    daddu(AT, AT, recv_klass);
    daddiu(recv_klass, AT, itentry_off);
  }

  Label search, found_method;

  for (int peel = 1; peel >= 0; peel--) {
    ld(method_result, Address(scan_temp, itableOffsetEntry::interface_offset_in_bytes()));

    if (peel) {
      beq(intf_klass, method_result, found_method);
      delayed()->nop();
    } else {
      bne(intf_klass, method_result, search);
      delayed()->nop();
      // (invert the test to fall through to found_method...)
    }

    if (!peel)  break;

    bind(search);

    // Check that the previous entry is non-null.  A null entry means that
    // the receiver class doesn't implement the interface, and wasn't the
    // same as when the caller was compiled.
    beq(method_result, R0, L_no_such_interface);
    delayed()->nop();
    daddiu(scan_temp, scan_temp, scan_step);
  }

  bind(found_method);

  if (return_method) {
    // Got a hit.
    lw(scan_temp, Address(scan_temp, itableOffsetEntry::offset_offset_in_bytes()));
    if (UseLEXT1) {
      gsldx(method_result, recv_klass, scan_temp, 0);
    } else {
      daddu(AT, recv_klass, scan_temp);
      ld(method_result, AT, 0);
    }
  }
}

// virtual method calling
void MacroAssembler::lookup_virtual_method(Register recv_klass,
                                           RegisterOrConstant vtable_index,
                                           Register method_result) {
  Register tmp = GP;
  push(tmp);

  if (vtable_index.is_constant()) {
    assert_different_registers(recv_klass, method_result, tmp);
  } else {
    assert_different_registers(recv_klass, method_result, vtable_index.as_register(), tmp);
  }
  const int base = in_bytes(Klass::vtable_start_offset());
  assert(vtableEntry::size() * wordSize == wordSize, "else adjust the scaling in the code below");
  if (vtable_index.is_constant()) {
    set64(AT, vtable_index.as_constant());
    dsll(AT, AT, (int)Address::times_ptr);
  } else {
    dsll(AT, vtable_index.as_register(), (int)Address::times_ptr);
  }
  set64(tmp, base + vtableEntry::method_offset_in_bytes());
  daddu(tmp, tmp, AT);
  daddu(tmp, tmp, recv_klass);
  ld(method_result, tmp, 0);

  pop(tmp);
}

void MacroAssembler::store_for_type_by_register(Register src_reg, Register tmp_reg, int disp, BasicType type, bool wide) {
  switch (type) {
    case T_LONG:
      st_ptr(src_reg, tmp_reg, disp);
      break;
    case T_ARRAY:
    case T_OBJECT:
      if (UseCompressedOops && !wide) {
        sw(src_reg, tmp_reg, disp);
      } else {
        st_ptr(src_reg, tmp_reg, disp);
      }
      break;
    case T_ADDRESS:
      st_ptr(src_reg, tmp_reg, disp);
      break;
    case T_INT:
      sw(src_reg, tmp_reg, disp);
      break;
    case T_CHAR:
    case T_SHORT:
      sh(src_reg, tmp_reg, disp);
      break;
    case T_BYTE:
    case T_BOOLEAN:
      sb(src_reg, tmp_reg, disp);
      break;
    default:
      ShouldNotReachHere();
  }
}

void MacroAssembler::store_for_type(Register src_reg, Address addr, BasicType type, bool wide) {
  Register tmp_reg = T9;
  Register index_reg = addr.index();
  if (index_reg == NOREG) {
    tmp_reg = NOREG;
  }

  int scale = addr.scale();
  if (tmp_reg != NOREG && scale >= 0) {
    dsll(tmp_reg, index_reg, scale);
  }

  int disp = addr.disp();
  bool disp_is_simm16 = true;
  if (!Assembler::is_simm16(disp)) {
    disp_is_simm16 = false;
  }

  Register base_reg = addr.base();
  if (tmp_reg != NOREG) {
    assert_different_registers(tmp_reg, base_reg, index_reg);
  }

  if (tmp_reg != NOREG) {
    daddu(tmp_reg, base_reg, tmp_reg);
    if (!disp_is_simm16) {
      move(tmp_reg, disp);
      daddu(tmp_reg, base_reg, tmp_reg);
    }
    store_for_type_by_register(src_reg, tmp_reg, disp_is_simm16 ? disp : 0, type, wide);
  } else {
    if (!disp_is_simm16) {
      tmp_reg = T9;
      assert_different_registers(tmp_reg, base_reg);
      move(tmp_reg, disp);
      daddu(tmp_reg, base_reg, tmp_reg);
    }
    store_for_type_by_register(src_reg, disp_is_simm16 ? base_reg : tmp_reg, disp_is_simm16 ? disp : 0, type, wide);
  }
}

void MacroAssembler::store_for_type_by_register(FloatRegister src_reg, Register tmp_reg, int disp, BasicType type) {
  switch (type) {
    case T_DOUBLE:
      sdc1(src_reg, tmp_reg, disp);
      break;
    case T_FLOAT:
      swc1(src_reg, tmp_reg, disp);
      break;
    default:
      ShouldNotReachHere();
  }
}

void MacroAssembler::store_for_type(FloatRegister src_reg, Address addr, BasicType type) {
  Register tmp_reg = T9;
  Register index_reg = addr.index();
  if (index_reg == NOREG) {
    tmp_reg = NOREG;
  }

  int scale = addr.scale();
  if (tmp_reg != NOREG && scale >= 0) {
    dsll(tmp_reg, index_reg, scale);
  }

  int disp = addr.disp();
  bool disp_is_simm16 = true;
  if (!Assembler::is_simm16(disp)) {
    disp_is_simm16 = false;
  }

  Register base_reg = addr.base();
  if (tmp_reg != NOREG) {
    assert_different_registers(tmp_reg, base_reg, index_reg);
  }

  if (tmp_reg != NOREG) {
    daddu(tmp_reg, base_reg, tmp_reg);
    if (!disp_is_simm16) {
      move(tmp_reg, disp);
      daddu(tmp_reg, base_reg, tmp_reg);
    }
    store_for_type_by_register(src_reg, tmp_reg, disp_is_simm16 ? disp : 0, type);
  } else {
    if (!disp_is_simm16) {
      tmp_reg = T9;
      assert_different_registers(tmp_reg, base_reg);
      move(tmp_reg, disp);
      daddu(tmp_reg, base_reg, tmp_reg);
    }
    store_for_type_by_register(src_reg, disp_is_simm16 ? base_reg : tmp_reg, disp_is_simm16 ? disp : 0, type);
  }
}

void MacroAssembler::load_for_type_by_register(Register dst_reg, Register tmp_reg, int disp, BasicType type, bool wide) {
  switch (type) {
    case T_LONG:
      ld_ptr(dst_reg, tmp_reg, disp);
      break;
    case T_ARRAY:
    case T_OBJECT:
      if (UseCompressedOops && !wide) {
        lwu(dst_reg, tmp_reg, disp);
      } else {
        ld_ptr(dst_reg, tmp_reg, disp);
      }
      break;
    case T_ADDRESS:
      if (UseCompressedClassPointers && disp == oopDesc::klass_offset_in_bytes()) {
        lwu(dst_reg, tmp_reg, disp);
      } else {
        ld_ptr(dst_reg, tmp_reg, disp);
      }
      break;
    case T_INT:
      lw(dst_reg, tmp_reg, disp);
      break;
    case T_CHAR:
      lhu(dst_reg, tmp_reg, disp);
      break;
    case T_SHORT:
      lh(dst_reg, tmp_reg, disp);
      break;
    case T_BYTE:
    case T_BOOLEAN:
      lb(dst_reg, tmp_reg, disp);
      break;
    default:
      ShouldNotReachHere();
  }
}

int MacroAssembler::load_for_type(Register dst_reg, Address addr, BasicType type, bool wide) {
  int code_offset = 0;
  Register tmp_reg = T9;
  Register index_reg = addr.index();
  if (index_reg == NOREG) {
    tmp_reg = NOREG;
  }

  int scale = addr.scale();
  if (tmp_reg != NOREG && scale >= 0) {
    dsll(tmp_reg, index_reg, scale);
  }

  int disp = addr.disp();
  bool disp_is_simm16 = true;
  if (!Assembler::is_simm16(disp)) {
    disp_is_simm16 = false;
  }

  Register base_reg = addr.base();
  if (tmp_reg != NOREG) {
    assert_different_registers(tmp_reg, base_reg, index_reg);
  }

  if (tmp_reg != NOREG) {
    daddu(tmp_reg, base_reg, tmp_reg);
    if (!disp_is_simm16) {
      move(tmp_reg, disp);
      daddu(tmp_reg, base_reg, tmp_reg);
    }
    code_offset = offset();
    load_for_type_by_register(dst_reg, tmp_reg, disp_is_simm16 ? disp : 0, type, wide);
  } else {
    if (!disp_is_simm16) {
      tmp_reg = T9;
      assert_different_registers(tmp_reg, base_reg);
      move(tmp_reg, disp);
      daddu(tmp_reg, base_reg, tmp_reg);
    }
    code_offset = offset();
    load_for_type_by_register(dst_reg, disp_is_simm16 ? base_reg : tmp_reg, disp_is_simm16 ? disp : 0, type, wide);
  }

  return code_offset;
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

  if (!str1_isL) srl(cnt1, cnt1, 1);
  if (!str2_isL) srl(cnt2, cnt2, 1);

  // compute the and difference of lengths (in result)
  subu(result, cnt1, cnt2); // result holds the difference of two lengths

  // compute the shorter length (in cnt1)
  slt(AT, cnt2, cnt1);
  movn(cnt1, cnt2, AT);

  // Now the shorter length is in cnt1 and cnt2 can be used as a tmp register
  bind(Loop);                        // Loop begin
  beq(cnt1, R0, done);
  if (str1_isL) {
    delayed()->lbu(AT, str1, 0);
  } else {
    delayed()->lhu(AT, str1, 0);
  }

  // compare current character
  if (str2_isL) {
    lbu(cnt2, str2, 0);
  } else {
    lhu(cnt2, str2, 0);
  }
  bne(AT, cnt2, haveResult);
  delayed()->addiu(str1, str1, str1_isL ? 1 : 2);
  addiu(str2, str2, str2_isL ? 1 : 2);
  b(Loop);
  delayed()->addiu(cnt1, cnt1, -1);   // Loop end

  bind(haveResult);
  subu(result, AT, cnt2);

  bind(done);
}

// Compare char[] or byte[] arrays or substrings.
void MacroAssembler::arrays_equals(Register str1, Register str2,
                                   Register cnt, Register tmp, Register result,
                                   bool is_char) {
  Label Loop, True, False;

  beq(str1, str2, True);  // same char[] ?
  delayed()->daddiu(result, R0, 1);

  beq(cnt, R0, True);
  delayed()->nop(); // count == 0

  bind(Loop);

  // compare current character
  if (is_char) {
    lhu(AT, str1, 0);
    lhu(tmp, str2, 0);
  } else {
    lbu(AT, str1, 0);
    lbu(tmp, str2, 0);
  }
  bne(AT, tmp, False);
  delayed()->addiu(str1, str1, is_char ? 2 : 1);
  addiu(cnt, cnt, -1);
  bne(cnt, R0, Loop);
  delayed()->addiu(str2, str2, is_char ? 2 : 1);

  b(True);
  delayed()->nop();

  bind(False);
  daddiu(result, R0, 0);

  bind(True);
}
#endif // COMPILER2

void MacroAssembler::load_for_type_by_register(FloatRegister dst_reg, Register tmp_reg, int disp, BasicType type) {
  switch (type) {
    case T_DOUBLE:
      ldc1(dst_reg, tmp_reg, disp);
      break;
    case T_FLOAT:
      lwc1(dst_reg, tmp_reg, disp);
      break;
    default:
      ShouldNotReachHere();
  }
}

int MacroAssembler::load_for_type(FloatRegister dst_reg, Address addr, BasicType type) {
  int code_offset = 0;
  Register tmp_reg = T9;
  Register index_reg = addr.index();
  if (index_reg == NOREG) {
    tmp_reg = NOREG;
  }

  int scale = addr.scale();
  if (tmp_reg != NOREG && scale >= 0) {
    dsll(tmp_reg, index_reg, scale);
  }

  int disp = addr.disp();
  bool disp_is_simm16 = true;
  if (!Assembler::is_simm16(disp)) {
    disp_is_simm16 = false;
  }

  Register base_reg = addr.base();
  if (tmp_reg != NOREG) {
    assert_different_registers(tmp_reg, base_reg, index_reg);
  }

  if (tmp_reg != NOREG) {
    daddu(tmp_reg, base_reg, tmp_reg);
    if (!disp_is_simm16) {
      move(tmp_reg, disp);
      daddu(tmp_reg, base_reg, tmp_reg);
    }
    code_offset = offset();
    load_for_type_by_register(dst_reg, tmp_reg, disp_is_simm16 ? disp : 0, type);
  } else {
    if (!disp_is_simm16) {
      tmp_reg = T9;
      assert_different_registers(tmp_reg, base_reg);
      move(tmp_reg, disp);
      daddu(tmp_reg, base_reg, tmp_reg);
    }
    code_offset = offset();
    load_for_type_by_register(dst_reg, disp_is_simm16 ? base_reg : tmp_reg, disp_is_simm16 ? disp : 0, type);
  }

  return code_offset;
}

void MacroAssembler::clear_jweak_tag(Register possibly_jweak) {
  const int32_t inverted_jweak_mask = ~static_cast<int32_t>(JNIHandles::weak_tag_mask);
  STATIC_ASSERT(inverted_jweak_mask == -2); // otherwise check this code
  // The inverted mask is sign-extended
  move(AT, inverted_jweak_mask);
  andr(possibly_jweak, AT, possibly_jweak);
}

void MacroAssembler::resolve_jobject(Register value,
                                     Register thread,
                                     Register tmp) {
  assert_different_registers(value, thread, tmp);
  Label done, not_weak;
  beq(value, R0, done);                // Use NULL as-is.
  delayed()->nop();
  move(AT, JNIHandles::weak_tag_mask); // Test for jweak tag.
  andr(AT, value, AT);
  beq(AT, R0, not_weak);
  delayed()->nop();
  // Resolve jweak.
  access_load_at(T_OBJECT, IN_NATIVE | ON_PHANTOM_OOP_REF,
                 value, Address(value, -JNIHandles::weak_tag_value), tmp, thread);
  verify_oop(value);
  b(done);
  delayed()->nop();
  bind(not_weak);
  // Resolve (untagged) jobject.
  access_load_at(T_OBJECT, IN_NATIVE, value, Address(value, 0), tmp, thread);
  verify_oop(value);
  bind(done);
}

void MacroAssembler::cmp_cmov(Register  op1,
                              Register  op2,
                              Register  dst,
                              Register  src,
                              CMCompare cmp,
                              bool      is_signed) {
  switch (cmp) {
    case EQ:
      subu(AT, op1, op2);
      movz(dst, src, AT);
      break;

    case NE:
      subu(AT, op1, op2);
      movn(dst, src, AT);
      break;

    case GT:
      if (is_signed) {
        slt(AT, op2, op1);
      } else {
        sltu(AT, op2, op1);
      }
      movn(dst, src, AT);
      break;

    case GE:
      if (is_signed) {
        slt(AT, op1, op2);
      } else {
        sltu(AT, op1, op2);
      }
      movz(dst, src, AT);
      break;

    case LT:
      if (is_signed) {
        slt(AT, op1, op2);
      } else {
        sltu(AT, op1, op2);
      }
      movn(dst, src, AT);
      break;

    case LE:
      if (is_signed) {
        slt(AT, op2, op1);
      } else {
        sltu(AT, op2, op1);
      }
      movz(dst, src, AT);
      break;

    default:
      Unimplemented();
  }
}

void MacroAssembler::cmp_cmov(FloatRegister op1,
                              FloatRegister op2,
                              Register      dst,
                              Register      src,
                              CMCompare     cmp,
                              bool          is_float) {
  switch(cmp) {
    case EQ:
      if (is_float) {
        c_eq_s(op1, op2);
      } else {
        c_eq_d(op1, op2);
      }
      movt(dst, src);
      break;

    case NE:
      if (is_float) {
        c_eq_s(op1, op2);
      } else {
        c_eq_d(op1, op2);
      }
      movf(dst, src);
      break;

    case GT:
      if (is_float) {
        c_ule_s(op1, op2);
      } else {
        c_ule_d(op1, op2);
      }
      movf(dst, src);
      break;

    case GE:
      if (is_float) {
        c_ult_s(op1, op2);
      } else {
        c_ult_d(op1, op2);
      }
      movf(dst, src);
      break;

    case LT:
      if (is_float) {
        c_ult_s(op1, op2);
      } else {
        c_ult_d(op1, op2);
      }
      movt(dst, src);
      break;

    case LE:
      if (is_float) {
        c_ule_s(op1, op2);
      } else {
        c_ule_d(op1, op2);
      }
      movt(dst, src);
      break;

    default:
      Unimplemented();
  }
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
        c_eq_d(op1, op2);
        movt_d(dst, src);
      } else {
        c_eq_s(op1, op2);
        movt_s(dst, src);
      }
      break;

    case NE:
      if (!is_float) {
        c_eq_d(op1, op2);
        movf_d(dst, src);
      } else {
        c_eq_s(op1, op2);
        movf_s(dst, src);
      }
      break;

    case GT:
      if (!is_float) {
        c_ule_d(op1, op2);
        movf_d(dst, src);
      } else {
        c_ule_s(op1, op2);
        movf_s(dst, src);
      }
      break;

    case GE:
      if (!is_float) {
        c_ult_d(op1, op2);
        movf_d(dst, src);
      } else {
        c_ult_s(op1, op2);
        movf_s(dst, src);
      }
      break;

    case LT:
      if (!is_float) {
        c_ult_d(op1, op2);
        movt_d(dst, src);
      } else {
        c_ult_s(op1, op2);
        movt_s(dst, src);
      }
      break;

    case LE:
      if (!is_float) {
        c_ule_d(op1, op2);
        movt_d(dst, src);
      } else {
        c_ule_s(op1, op2);
        movt_s(dst, src);
      }
      break;

    default:
      Unimplemented();
  }
}

void MacroAssembler::cmp_cmov(Register      op1,
                              Register      op2,
                              FloatRegister dst,
                              FloatRegister src,
                              CMCompare     cmp,
                              bool          is_float) {
  Label L;

  switch(cmp) {
    case EQ:
      bne(op1, op2, L);
      delayed()->nop();
      if (is_float) {
        mov_s(dst, src);
      } else {
        mov_d(dst, src);
      }
      bind(L);
      break;

    case NE:
      beq(op1, op2, L);
      delayed()->nop();
      if (is_float) {
        mov_s(dst, src);
      } else {
        mov_d(dst, src);
      }
      bind(L);
      break;

    case GT:
      slt(AT, op2, op1);
      beq(AT, R0, L);
      delayed()->nop();
      if (is_float) {
        mov_s(dst, src);
      } else {
        mov_d(dst, src);
      }
      bind(L);
      break;

    case GE:
      slt(AT, op1, op2);
      bne(AT, R0, L);
      delayed()->nop();
      if (is_float) {
        mov_s(dst, src);
      } else {
        mov_d(dst, src);
      }
      bind(L);
      break;

    case LT:
      slt(AT, op1, op2);
      beq(AT, R0, L);
      delayed()->nop();
      if (is_float) {
        mov_s(dst, src);
      } else {
        mov_d(dst, src);
      }
      bind(L);
      break;

    case LE:
      slt(AT, op2, op1);
      bne(AT, R0, L);
      delayed()->nop();
      if (is_float) {
        mov_s(dst, src);
      } else {
        mov_d(dst, src);
      }
      bind(L);
      break;

    default:
      Unimplemented();
  }
}

void MacroAssembler::gs_loadstore(Register reg, Register base, Register index, int disp, int type) {
  switch (type) {
    case STORE_BYTE:
      gssbx(reg, base, index, disp);
      break;
    case STORE_CHAR:
    case STORE_SHORT:
      gsshx(reg, base, index, disp);
      break;
    case STORE_INT:
      gsswx(reg, base, index, disp);
      break;
    case STORE_LONG:
      gssdx(reg, base, index, disp);
      break;
    case LOAD_BYTE:
      gslbx(reg, base, index, disp);
      break;
    case LOAD_SHORT:
      gslhx(reg, base, index, disp);
      break;
    case LOAD_INT:
      gslwx(reg, base, index, disp);
      break;
    case LOAD_LONG:
      gsldx(reg, base, index, disp);
      break;
    default:
      ShouldNotReachHere();
  }
}

void MacroAssembler::gs_loadstore(FloatRegister reg, Register base, Register index, int disp, int type) {
  switch (type) {
    case STORE_FLOAT:
      gsswxc1(reg, base, index, disp);
      break;
    case STORE_DOUBLE:
      gssdxc1(reg, base, index, disp);
      break;
    case LOAD_FLOAT:
      gslwxc1(reg, base, index, disp);
      break;
    case LOAD_DOUBLE:
      gsldxc1(reg, base, index, disp);
      break;
    default:
      ShouldNotReachHere();
  }
}

void MacroAssembler::loadstore(Register reg, Register base, int disp, int type) {
  switch (type) {
    case STORE_BYTE:
      sb(reg, base, disp);
      break;
    case STORE_CHAR:
    case STORE_SHORT:
      sh(reg, base, disp);
      break;
    case STORE_INT:
      sw(reg, base, disp);
      break;
    case STORE_LONG:
      sd(reg, base, disp);
      break;
    case LOAD_BYTE:
      lb(reg, base, disp);
      break;
    case LOAD_U_BYTE:
      lbu(reg, base, disp);
      break;
    case LOAD_SHORT:
      lh(reg, base, disp);
      break;
    case LOAD_U_SHORT:
      lhu(reg, base, disp);
      break;
    case LOAD_INT:
      lw(reg, base, disp);
      break;
    case LOAD_U_INT:
      lwu(reg, base, disp);
      break;
    case LOAD_LONG:
      ld(reg, base, disp);
      break;
    case LOAD_LINKED_LONG:
      lld(reg, base, disp);
      break;
     default:
       ShouldNotReachHere();
    }
}

void MacroAssembler::loadstore(FloatRegister reg, Register base, int disp, int type) {
  switch (type) {
    case STORE_FLOAT:
      swc1(reg, base, disp);
      break;
    case STORE_DOUBLE:
      sdc1(reg, base, disp);
      break;
    case LOAD_FLOAT:
      lwc1(reg, base, disp);
      break;
    case LOAD_DOUBLE:
      ldc1(reg, base, disp);
      break;
     default:
       ShouldNotReachHere();
    }
}
