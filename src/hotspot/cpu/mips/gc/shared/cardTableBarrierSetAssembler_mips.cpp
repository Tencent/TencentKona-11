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
#include "gc/shared/barrierSet.hpp"
#include "gc/shared/cardTable.hpp"
#include "gc/shared/cardTableBarrierSet.hpp"
#include "gc/shared/cardTableBarrierSetAssembler.hpp"

#define __ masm->

#define T9 RT9

#ifdef PRODUCT
#define BLOCK_COMMENT(str) /* nothing */
#else
#define BLOCK_COMMENT(str) __ block_comment(str)
#endif

#define BIND(label) bind(label); BLOCK_COMMENT(#label ":")

#define TIMES_OOP (UseCompressedOops ? Address::times_4 : Address::times_8)

void CardTableBarrierSetAssembler::gen_write_ref_array_post_barrier(MacroAssembler* masm, DecoratorSet decorators,
                                                                    Register addr, Register count, Register tmp) {
  BarrierSet *bs = BarrierSet::barrier_set();
  CardTableBarrierSet* ctbs = barrier_set_cast<CardTableBarrierSet>(bs);
  CardTable* ct = ctbs->card_table();
  assert(sizeof(*ct->byte_map_base()) == sizeof(jbyte), "adjust this code");
  intptr_t disp = (intptr_t) ct->byte_map_base();

  Label L_loop, L_done;
  const Register end = count;
  assert_different_registers(addr, end);

  __ beq(count, R0, L_done); // zero count - nothing to do
  __ delayed()->nop();

  if (UseConcMarkSweepGC) __ sync();

  __ set64(tmp, disp);

  __ lea(end, Address(addr, count, TIMES_OOP, 0));  // end == addr+count*oop_size
  __ daddiu(end, end, -BytesPerHeapOop); // end - 1 to make inclusive
  __ shr(addr, CardTable::card_shift);
  __ shr(end, CardTable::card_shift);
  __ dsubu(end, end, addr); // end --> cards count

  __ daddu(addr, addr, tmp);

  __ BIND(L_loop);
  if (UseLEXT1) {
    __ gssbx(R0, addr, count, 0);
  } else {
    __ daddu(AT, addr, count);
    __ sb(R0, AT, 0);
  }
  __ daddiu(count, count, -1);
  __ bgez(count, L_loop);
  __ delayed()->nop();

  __ BIND(L_done);
}

void CardTableBarrierSetAssembler::store_check(MacroAssembler* masm, Register obj, Address dst) {
  // Does a store check for the oop in register obj. The content of
  // register obj is destroyed afterwards.
  BarrierSet* bs = BarrierSet::barrier_set();

  CardTableBarrierSet* ctbs = barrier_set_cast<CardTableBarrierSet>(bs);
  CardTable* ct = ctbs->card_table();
  assert(sizeof(*ct->byte_map_base()) == sizeof(jbyte), "adjust this code");

  __ shr(obj, CardTable::card_shift);

  Address card_addr;

  intptr_t byte_map_base = (intptr_t)ct->byte_map_base();
  Register tmp = T9;
  assert_different_registers(tmp, obj);
  __ li(tmp, byte_map_base);
  __ addu(tmp, tmp, obj);

  assert(CardTable::dirty_card_val() == 0, "must be");

  jbyte dirty = CardTable::dirty_card_val();
  if (UseCondCardMark) {
    Untested("Untested");
    __ warn("store_check Untested");
    Label L_already_dirty;
    __ membar(Assembler::StoreLoad);
    __ lb(AT, tmp, 0);
    __ addiu(AT, AT, -1 * dirty);
    __ beq(AT, R0, L_already_dirty);
    __ delayed()->nop();
    __ sb(R0, tmp, 0);
    __ bind(L_already_dirty);
  } else {
    if (ct->scanned_concurrently()) {
      __ membar(Assembler::StoreLoad);
    }
    __ sb(R0, tmp, 0);
  }
}

void CardTableBarrierSetAssembler::oop_store_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                                                Address dst, Register val, Register tmp1, Register tmp2) {
  bool in_heap = (decorators & IN_HEAP) != 0;

  bool is_array = (decorators & IS_ARRAY) != 0;
  bool on_anonymous = (decorators & ON_UNKNOWN_OOP_REF) != 0;
  bool precise = is_array || on_anonymous;

  bool needs_post_barrier = val != noreg && in_heap;

  BarrierSetAssembler::store_at(masm, decorators, type, dst, val, noreg, noreg);
  if (needs_post_barrier) {
    // flatten object address if needed
    if (!precise || (dst.index() == noreg && dst.disp() == 0)) {
      store_check(masm, dst.base(), dst);
    } else {
      __ lea(tmp1, dst);
      store_check(masm, tmp1, dst);
    }
  }
}
