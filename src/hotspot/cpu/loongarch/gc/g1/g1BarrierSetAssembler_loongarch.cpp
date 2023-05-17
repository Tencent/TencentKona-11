/*
 * Copyright (c) 2018, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2021, Loongson Technology. All rights reserved.
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
#include "gc/g1/g1BarrierSet.hpp"
#include "gc/g1/g1BarrierSetAssembler.hpp"
#include "gc/g1/g1BarrierSetRuntime.hpp"
#include "gc/g1/g1CardTable.hpp"
#include "gc/g1/g1ThreadLocalData.hpp"
#include "gc/g1/heapRegion.hpp"
#include "interpreter/interp_masm.hpp"
#include "runtime/sharedRuntime.hpp"
#include "utilities/macros.hpp"
#ifdef COMPILER1
#include "c1/c1_LIRAssembler.hpp"
#include "c1/c1_MacroAssembler.hpp"
#include "gc/g1/c1/g1BarrierSetC1.hpp"
#endif

#define __ masm->

void G1BarrierSetAssembler::gen_write_ref_array_pre_barrier(MacroAssembler* masm, DecoratorSet decorators,
                                                            Register addr, Register count, RegSet saved_regs) {
  bool dest_uninitialized = (decorators & IS_DEST_UNINITIALIZED) != 0;

  if (!dest_uninitialized) {
#ifndef OPT_THREAD
    Register thread = T9;
    __ get_thread(thread);
#else
    Register thread = TREG;
#endif

    Label filtered;
    Address in_progress(thread, in_bytes(G1ThreadLocalData::satb_mark_queue_active_offset()));
    // Is marking active?
    if (in_bytes(SATBMarkQueue::byte_width_of_active()) == 4) {
      __ ld_w(AT, in_progress);
    } else {
      assert(in_bytes(SATBMarkQueue::byte_width_of_active()) == 1, "Assumption");
      __ ld_b(AT, in_progress);
    }

    __ beqz(AT, filtered);

    __ push(saved_regs);
    if (count == A0) {
      if (addr == A1) {
        __ move(AT, A0);
        __ move(A0, A1);
        __ move(A1, AT);
      } else {
        __ move(A1, count);
        __ move(A0, addr);
      }
    } else {
      __ move(A0, addr);
      __ move(A1, count);
    }
    if (UseCompressedOops) {
      __ call_VM_leaf(CAST_FROM_FN_PTR(address, G1BarrierSetRuntime::write_ref_array_pre_narrow_oop_entry), 2);
    } else {
      __ call_VM_leaf(CAST_FROM_FN_PTR(address, G1BarrierSetRuntime::write_ref_array_pre_oop_entry), 2);
    }
    __ pop(saved_regs);

    __ bind(filtered);
  }
}

void G1BarrierSetAssembler::gen_write_ref_array_post_barrier(MacroAssembler* masm, DecoratorSet decorators,
                                                             Register addr, Register count, Register tmp, RegSet saved_regs) {
  __ push(saved_regs);
  if (count == A0) {
    assert_different_registers(A1, addr);
    __ move(A1, count);
    __ move(A0, addr);
  } else {
    assert_different_registers(A0, count);
    __ move(A0, addr);
    __ move(A1, count);
  }
  __ call_VM_leaf(CAST_FROM_FN_PTR(address, G1BarrierSetRuntime::write_ref_array_post_entry), 2);
  __ pop(saved_regs);
}

void G1BarrierSetAssembler::load_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                                    Register dst, Address src, Register tmp1, Register tmp_thread) {
  bool on_oop = type == T_OBJECT || type == T_ARRAY;
  bool on_weak = (decorators & ON_WEAK_OOP_REF) != 0;
  bool on_phantom = (decorators & ON_PHANTOM_OOP_REF) != 0;
  bool on_reference = on_weak || on_phantom;
  ModRefBarrierSetAssembler::load_at(masm, decorators, type, dst, src, tmp1, tmp_thread);
  if (on_oop && on_reference) {
    const Register thread = TREG;
#ifndef OPT_THREAD
    __ get_thread(thread);
#endif
    // RA is live. It must be saved around calls.
    __ enter(); // barrier may call runtime
    // Generate the G1 pre-barrier code to log the value of
    // the referent field in an SATB buffer.
    g1_write_barrier_pre(masm /* masm */,
                         noreg /* obj */,
                         dst /* pre_val */,
                         thread /* thread */,
                         tmp1 /* tmp */,
                         true /* tosca_live */,
                         true /* expand_call */);
    __ leave();
  }
}

void G1BarrierSetAssembler::g1_write_barrier_pre(MacroAssembler* masm,
                                                 Register obj,
                                                 Register pre_val,
                                                 Register thread,
                                                 Register tmp,
                                                 bool tosca_live,
                                                 bool expand_call) {
  // If expand_call is true then we expand the call_VM_leaf macro
  // directly to skip generating the check by
  // InterpreterMacroAssembler::call_VM_leaf_base that checks _last_sp.

  assert(thread == TREG, "must be");

  Label done;
  Label runtime;

  assert(pre_val != noreg, "check this code");

  if (obj != noreg) {
    assert_different_registers(obj, pre_val, tmp);
    assert(pre_val != V0, "check this code");
  }

  Address in_progress(thread, in_bytes(G1ThreadLocalData::satb_mark_queue_active_offset()));
  Address index(thread, in_bytes(G1ThreadLocalData::satb_mark_queue_index_offset()));
  Address buffer(thread, in_bytes(G1ThreadLocalData::satb_mark_queue_buffer_offset()));

  // Is marking active?
  if (in_bytes(SATBMarkQueue::byte_width_of_active()) == 4) {
    __ ld_w(AT, in_progress);
  } else {
    assert(in_bytes(SATBMarkQueue::byte_width_of_active()) == 1, "Assumption");
    __ ld_b(AT, in_progress);
  }
  __ beqz(AT, done);

  // Do we need to load the previous value?
  if (obj != noreg) {
    __ load_heap_oop(pre_val, Address(obj, 0));
  }

  // Is the previous value null?
  __ beqz(pre_val, done);

  // Can we store original value in the thread's buffer?
  // Is index == 0?
  // (The index field is typed as size_t.)

  __ ld_d(tmp, index);
  __ beqz(tmp, runtime);

  __ addi_d(tmp, tmp, -1 * wordSize);
  __ st_d(tmp, index);
  __ ld_d(AT, buffer);

  // Record the previous value
  __ stx_d(pre_val, tmp, AT);
  __ b(done);

  __ bind(runtime);
  // save the live input values
  if (tosca_live) __ push(V0);

  if (obj != noreg && obj != V0) __ push(obj);

  if (pre_val != V0) __ push(pre_val);

  // Calling the runtime using the regular call_VM_leaf mechanism generates
  // code (generated by InterpreterMacroAssember::call_VM_leaf_base)
  // that checks that the *(ebp+frame::interpreter_frame_last_sp) == NULL.
  //
  // If we care generating the pre-barrier without a frame (e.g. in the
  // intrinsified Reference.get() routine) then ebp might be pointing to
  // the caller frame and so this check will most likely fail at runtime.
  //
  // Expanding the call directly bypasses the generation of the check.
  // So when we do not have have a full interpreter frame on the stack
  // expand_call should be passed true.

  if (expand_call) {
    assert(pre_val != A1, "smashed arg");
    if (thread != A1) __ move(A1, thread);
    if (pre_val != A0) __ move(A0, pre_val);
    __ super_call_VM_leaf(CAST_FROM_FN_PTR(address, G1BarrierSetRuntime::write_ref_field_pre_entry), pre_val, thread);
  } else {
    __ call_VM_leaf(CAST_FROM_FN_PTR(address, G1BarrierSetRuntime::write_ref_field_pre_entry), pre_val, thread);
  }

  // save the live input values
  if (pre_val != V0)
    __ pop(pre_val);

  if (obj != noreg && obj != V0)
    __ pop(obj);

  if (tosca_live) __ pop(V0);

  __ bind(done);
}

void G1BarrierSetAssembler::g1_write_barrier_post(MacroAssembler* masm,
                                                  Register store_addr,
                                                  Register new_val,
                                                  Register thread,
                                                  Register tmp,
                                                  Register tmp2) {
  assert_different_registers(tmp, tmp2, AT);
  assert(thread == TREG, "must be");

  Address queue_index(thread, in_bytes(G1ThreadLocalData::dirty_card_queue_index_offset()));
  Address buffer(thread, in_bytes(G1ThreadLocalData::dirty_card_queue_buffer_offset()));

  CardTableBarrierSet* ct = barrier_set_cast<CardTableBarrierSet>(BarrierSet::barrier_set());
  assert(sizeof(*ct->card_table()->byte_map_base()) == sizeof(jbyte), "adjust this code");

  Label done;
  Label runtime;

  // Does store cross heap regions?
  __ xorr(AT, store_addr, new_val);
  __ srli_d(AT, AT, HeapRegion::LogOfHRGrainBytes);
  __ beqz(AT, done);

  // crosses regions, storing NULL?
  __ beqz(new_val, done);

  // storing region crossing non-NULL, is card already dirty?
  const Register card_addr = tmp;
  const Register cardtable = tmp2;

  __ move(card_addr, store_addr);
  __ srli_d(card_addr, card_addr, CardTable::card_shift);
  // Do not use ExternalAddress to load 'byte_map_base', since 'byte_map_base' is NOT
  // a valid address and therefore is not properly handled by the relocation code.
  __ li(cardtable, (intptr_t)ct->card_table()->byte_map_base());
  __ add_d(card_addr, card_addr, cardtable);

  __ ld_bu(AT, card_addr, 0);
  __ addi_d(AT, AT, -1 * (int)G1CardTable::g1_young_card_val());
  __ beqz(AT, done);

  assert((int)CardTable::dirty_card_val() == 0, "must be 0");

  __ membar(__ StoreLoad);
  __ ld_bu(AT, card_addr, 0);
  __ beqz(AT, done);

  // storing a region crossing, non-NULL oop, card is clean.
  // dirty card and log.
  __ st_b(R0, card_addr, 0);

  __ ld_d(AT, queue_index);
  __ beqz(AT, runtime);
  __ addi_d(AT, AT, -1 * wordSize);
  __ st_d(AT, queue_index);
  __ ld_d(tmp2, buffer);
  __ ld_d(AT, queue_index);
  __ stx_d(card_addr, tmp2, AT);
  __ b(done);

  __ bind(runtime);
  // save the live input values
  __ push(store_addr);
  __ push(new_val);
  __ call_VM_leaf(CAST_FROM_FN_PTR(address, G1BarrierSetRuntime::write_ref_field_post_entry), card_addr, TREG);
  __ pop(new_val);
  __ pop(store_addr);

  __ bind(done);
}

void G1BarrierSetAssembler::oop_store_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                                         Address dst, Register val, Register tmp1, Register tmp2) {
  bool in_heap = (decorators & IN_HEAP) != 0;
  bool as_normal = (decorators & AS_NORMAL) != 0;
  assert((decorators & IS_DEST_UNINITIALIZED) == 0, "unsupported");

  bool needs_pre_barrier = as_normal;
  bool needs_post_barrier = val != noreg && in_heap;

  Register tmp3 = RT3;
  Register rthread = TREG;
  // flatten object address if needed
  // We do it regardless of precise because we need the registers
  if (dst.index() == noreg && dst.disp() == 0) {
    if (dst.base() != tmp3) {
      __ move(tmp3, dst.base());
    }
  } else {
    __ lea(tmp3, dst);
  }

  if (needs_pre_barrier) {
    g1_write_barrier_pre(masm /*masm*/,
                         tmp3 /* obj */,
                         tmp2 /* pre_val */,
                         rthread /* thread */,
                         tmp1  /* tmp */,
                         val != noreg /* tosca_live */,
                         false /* expand_call */);
  }
  if (val == noreg) {
    BarrierSetAssembler::store_at(masm, decorators, type, Address(tmp3, 0), val, noreg, noreg);
  } else {
    Register new_val = val;
    if (needs_post_barrier) {
      // G1 barrier needs uncompressed oop for region cross check.
      if (UseCompressedOops) {
        new_val = tmp2;
        __ move(new_val, val);
      }
    }
    BarrierSetAssembler::store_at(masm, decorators, type, Address(tmp3, 0), val, noreg, noreg);
    if (needs_post_barrier) {
      g1_write_barrier_post(masm /*masm*/,
                            tmp3 /* store_adr */,
                            new_val /* new_val */,
                            rthread /* thread */,
                            tmp1 /* tmp */,
                            tmp2 /* tmp2 */);
    }
  }
}

#ifdef COMPILER1

#undef __
#define __ ce->masm()->

void G1BarrierSetAssembler::gen_pre_barrier_stub(LIR_Assembler* ce, G1PreBarrierStub* stub) {
  G1BarrierSetC1* bs = (G1BarrierSetC1*)BarrierSet::barrier_set()->barrier_set_c1();
  // At this point we know that marking is in progress.
  // If do_load() is true then we have to emit the
  // load of the previous value; otherwise it has already
  // been loaded into _pre_val.

  __ bind(*stub->entry());

  assert(stub->pre_val()->is_register(), "Precondition.");

  Register pre_val_reg = stub->pre_val()->as_register();

  if (stub->do_load()) {
    ce->mem2reg(stub->addr(), stub->pre_val(), T_OBJECT, stub->patch_code(), stub->info(), false /*wide*/, false /*unaligned*/);
  }
  __ beqz(pre_val_reg, *stub->continuation());
  ce->store_parameter(stub->pre_val()->as_register(), 0);
  __ call(bs->pre_barrier_c1_runtime_code_blob()->code_begin(), relocInfo::runtime_call_type);
  __ b(*stub->continuation());
}

void G1BarrierSetAssembler::gen_post_barrier_stub(LIR_Assembler* ce, G1PostBarrierStub* stub) {
  G1BarrierSetC1* bs = (G1BarrierSetC1*)BarrierSet::barrier_set()->barrier_set_c1();
  __ bind(*stub->entry());
  assert(stub->addr()->is_register(), "Precondition.");
  assert(stub->new_val()->is_register(), "Precondition.");
  Register new_val_reg = stub->new_val()->as_register();
  __ beqz(new_val_reg, *stub->continuation());
  ce->store_parameter(stub->addr()->as_pointer_register(), 0);
  __ call(bs->post_barrier_c1_runtime_code_blob()->code_begin(), relocInfo::runtime_call_type);
  __ b(*stub->continuation());
}

#undef __

#define __ sasm->

void G1BarrierSetAssembler::generate_c1_pre_barrier_runtime_stub(StubAssembler* sasm) {
  __ prologue("g1_pre_barrier", false);

  // arg0 : previous value of memory

  BarrierSet* bs = BarrierSet::barrier_set();

  const Register pre_val = A0;
  const Register thread = TREG;
  const Register tmp = SCR2;

  Address in_progress(thread, in_bytes(G1ThreadLocalData::satb_mark_queue_active_offset()));
  Address queue_index(thread, in_bytes(G1ThreadLocalData::satb_mark_queue_index_offset()));
  Address buffer(thread, in_bytes(G1ThreadLocalData::satb_mark_queue_buffer_offset()));

  Label done;
  Label runtime;

  // Is marking still active?
  if (in_bytes(SATBMarkQueue::byte_width_of_active()) == 4) {
    __ ld_w(tmp, in_progress);
  } else {
    assert(in_bytes(SATBMarkQueue::byte_width_of_active()) == 1, "Assumption");
    __ ld_b(tmp, in_progress);
  }
  __ beqz(tmp, done);

  // Can we store original value in the thread's buffer?
  __ ld_ptr(tmp, queue_index);
  __ beqz(tmp, runtime);

  __ addi_d(tmp, tmp, -wordSize);
  __ st_ptr(tmp, queue_index);
  __ ld_ptr(SCR1, buffer);
  __ add_d(tmp, tmp, SCR1);
  __ load_parameter(0, SCR1);
  __ st_ptr(SCR1, Address(tmp, 0));
  __ b(done);

  __ bind(runtime);
  __ pushad();
  __ load_parameter(0, pre_val);
  __ call_VM_leaf(CAST_FROM_FN_PTR(address, G1BarrierSetRuntime::write_ref_field_pre_entry), pre_val, thread);
  __ popad();
  __ bind(done);

  __ epilogue();
}

void G1BarrierSetAssembler::generate_c1_post_barrier_runtime_stub(StubAssembler* sasm) {
  __ prologue("g1_post_barrier", false);

  // arg0: store_address
  Address store_addr(FP, 2 * BytesPerWord);

  BarrierSet* bs = BarrierSet::barrier_set();
  CardTableBarrierSet* ctbs = barrier_set_cast<CardTableBarrierSet>(bs);
  CardTable* ct = ctbs->card_table();

  Label done;
  Label runtime;

  // At this point we know new_value is non-NULL and the new_value crosses regions.
  // Must check to see if card is already dirty

  const Register thread = TREG;

  Address queue_index(thread, in_bytes(G1ThreadLocalData::dirty_card_queue_index_offset()));
  Address buffer(thread, in_bytes(G1ThreadLocalData::dirty_card_queue_buffer_offset()));

  const Register card_offset = SCR2;
  // RA is free here, so we can use it to hold the byte_map_base.
  const Register byte_map_base = RA;

  assert_different_registers(card_offset, byte_map_base, SCR1);

  __ load_parameter(0, card_offset);
  __ srli_d(card_offset, card_offset, CardTable::card_shift);
  __ load_byte_map_base(byte_map_base);
  __ ldx_bu(SCR1, byte_map_base, card_offset);
  __ addi_d(SCR1, SCR1, -(int)G1CardTable::g1_young_card_val());
  __ beqz(SCR1, done);

  assert((int)CardTable::dirty_card_val() == 0, "must be 0");

  __ membar(__ StoreLoad);
  __ ldx_bu(SCR1, byte_map_base, card_offset);
  __ beqz(SCR1, done);

  // storing region crossing non-NULL, card is clean.
  // dirty card and log.
  __ stx_b(R0, byte_map_base, card_offset);

  // Convert card offset into an address in card_addr
  Register card_addr = card_offset;
  __ add_d(card_addr, byte_map_base, card_addr);

  __ ld_ptr(SCR1, queue_index);
  __ beqz(SCR1, runtime);
  __ addi_d(SCR1, SCR1, -wordSize);
  __ st_ptr(SCR1, queue_index);

  // Reuse RA to hold buffer_addr
  const Register buffer_addr = RA;

  __ ld_ptr(buffer_addr, buffer);
  __ stx_d(card_addr, buffer_addr, SCR1);
  __ b(done);

  __ bind(runtime);
  __ pushad();
  __ call_VM_leaf(CAST_FROM_FN_PTR(address, G1BarrierSetRuntime::write_ref_field_post_entry), card_addr, thread);
  __ popad();
  __ bind(done);
  __ epilogue();
}

#undef __

#endif // COMPILER1
