/*
 * Copyright (c) 2018, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2018, 2022, Loongson Technology. All rights reserved.
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

#define __ masm->

void G1BarrierSetAssembler::gen_write_ref_array_pre_barrier(MacroAssembler* masm, DecoratorSet decorators,
                                                            Register addr, Register count) {
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
      __ lw(AT, in_progress);
    } else {
      assert(in_bytes(SATBMarkQueue::byte_width_of_active()) == 1, "Assumption");
      __ lb(AT, in_progress);
    }

    __ beq(AT, R0, filtered);
    __ delayed()->nop();

    __ pushad();                      // push registers
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
    __ popad();

    __ bind(filtered);
  }
}

void G1BarrierSetAssembler::gen_write_ref_array_post_barrier(MacroAssembler* masm, DecoratorSet decorators,
                                                             Register addr, Register count, Register tmp) {
  __ pushad();             // push registers (overkill)
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
  __ popad();
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
    // Generate the G1 pre-barrier code to log the value of
    // the referent field in an SATB buffer.
    g1_write_barrier_pre(masm /* masm */,
                         noreg /* obj */,
                         dst /* pre_val */,
                         thread /* thread */,
                         tmp1 /* tmp */,
                         true /* tosca_live */,
                         true /* expand_call */);
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
    __ lw(AT, in_progress);
  } else {
    assert(in_bytes(SATBMarkQueue::byte_width_of_active()) == 1, "Assumption");
    __ lb(AT, in_progress);
  }
  __ beq(AT, R0, done);
  __ delayed()->nop();

  // Do we need to load the previous value?
  if (obj != noreg) {
    __ load_heap_oop(pre_val, Address(obj, 0));
  }

  // Is the previous value null?
  __ beq(pre_val, R0, done);
  __ delayed()->nop();

  // Can we store original value in the thread's buffer?
  // Is index == 0?
  // (The index field is typed as size_t.)

  __ ld(tmp, index);
  __ beq(tmp, R0, runtime);
  __ delayed()->nop();

  __ daddiu(tmp, tmp, -1 * wordSize);
  __ sd(tmp, index);
  __ ld(AT, buffer);
  __ daddu(tmp, tmp, AT);

  // Record the previous value
  __ sd(pre_val, tmp, 0);
  __ beq(R0, R0, done);
  __ delayed()->nop();

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
  __ dsrl(AT, AT, HeapRegion::LogOfHRGrainBytes);
  __ beq(AT, R0, done);
  __ delayed()->nop();

  // crosses regions, storing NULL?
  __ beq(new_val, R0, done);
  __ delayed()->nop();

  // storing region crossing non-NULL, is card already dirty?
  const Register card_addr = tmp;
  const Register cardtable = tmp2;

  __ move(card_addr, store_addr);
  __ dsrl(card_addr, card_addr, CardTable::card_shift);
  // Do not use ExternalAddress to load 'byte_map_base', since 'byte_map_base' is NOT
  // a valid address and therefore is not properly handled by the relocation code.
  __ set64(cardtable, (intptr_t)ct->card_table()->byte_map_base());
  __ daddu(card_addr, card_addr, cardtable);

  __ lb(AT, card_addr, 0);
  __ daddiu(AT, AT, -1 * (int)G1CardTable::g1_young_card_val());
  __ beq(AT, R0, done);
  __ delayed()->nop();

  __ sync();
  __ lb(AT, card_addr, 0);
  __ daddiu(AT, AT, -1 * (int)G1CardTable::dirty_card_val());
  __ beq(AT, R0, done);
  __ delayed()->nop();

  // storing a region crossing, non-NULL oop, card is clean.
  // dirty card and log.
  __ move(AT, (int)G1CardTable::dirty_card_val());
  __ sb(AT, card_addr, 0);

  __ lw(AT, queue_index);
  __ beq(AT, R0, runtime);
  __ delayed()->nop();
  __ daddiu(AT, AT, -1 * wordSize);
  __ sw(AT, queue_index);
  __ ld(tmp2, buffer);
  __ ld(AT, queue_index);
  __ daddu(tmp2, tmp2, AT);
  __ sd(card_addr, tmp2, 0);
  __ beq(R0, R0, done);
  __ delayed()->nop();

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
