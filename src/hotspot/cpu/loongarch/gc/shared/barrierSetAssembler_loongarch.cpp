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
#include "gc/shared/barrierSetAssembler.hpp"
#include "gc/shared/collectedHeap.hpp"
#include "interpreter/interp_masm.hpp"
#include "runtime/jniHandles.hpp"
#include "runtime/thread.hpp"

#define __ masm->

void BarrierSetAssembler::load_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                                  Register dst, Address src, Register tmp1, Register tmp_thread) {
  bool in_heap = (decorators & IN_HEAP) != 0;
  bool in_native = (decorators & IN_NATIVE) != 0;
  bool is_not_null = (decorators & IS_NOT_NULL) != 0;

  switch (type) {
  case T_OBJECT:
  case T_ARRAY: {
    if (in_heap) {
      if (UseCompressedOops) {
        __ ld_wu(dst, src);
        if (is_not_null) {
          __ decode_heap_oop_not_null(dst);
        } else {
          __ decode_heap_oop(dst);
        }
      } else
      {
        __ ld_ptr(dst, src);
      }
    } else {
      assert(in_native, "why else?");
      __ ld_ptr(dst, src);
    }
    break;
  }
  case T_BOOLEAN: __ ld_bu   (dst, src);    break;
  case T_BYTE:    __ ld_b    (dst, src);    break;
  case T_CHAR:    __ ld_hu   (dst, src);    break;
  case T_SHORT:   __ ld_h    (dst, src);    break;
  case T_INT:     __ ld_w    (dst, src);    break;
  case T_LONG:    __ ld_d    (dst, src);    break;
  case T_ADDRESS: __ ld_ptr(dst, src);    break;
  case T_FLOAT:
    assert(dst == noreg, "only to ftos");
    __ fld_s(FSF, src);
    break;
  case T_DOUBLE:
    assert(dst == noreg, "only to dtos");
    __ fld_d(FSF, src);
    break;
  default: Unimplemented();
  }
}

void BarrierSetAssembler::store_at(MacroAssembler* masm, DecoratorSet decorators, BasicType type,
                                   Address dst, Register val, Register tmp1, Register tmp2) {
  bool in_heap = (decorators & IN_HEAP) != 0;
  bool in_native = (decorators & IN_NATIVE) != 0;
  bool is_not_null = (decorators & IS_NOT_NULL) != 0;

  switch (type) {
  case T_OBJECT:
  case T_ARRAY: {
    if (in_heap) {
      if (val == noreg) {
        assert(!is_not_null, "inconsistent access");
        if (UseCompressedOops) {
          __ st_w(R0, dst);
        } else {
          __ st_d(R0, dst);
        }
      } else {
        if (UseCompressedOops) {
          assert(!dst.uses(val), "not enough registers");
          if (is_not_null) {
            __ encode_heap_oop_not_null(val);
          } else {
            __ encode_heap_oop(val);
          }
          __ st_w(val, dst);
        } else
        {
          __ st_ptr(val, dst);
        }
      }
    } else {
      assert(in_native, "why else?");
      assert(val != noreg, "not supported");
      __ st_ptr(val, dst);
    }
    break;
  }
  case T_BOOLEAN:
    __ andi(val, val, 0x1);  // boolean is true if LSB is 1
    __ st_b(val, dst);
    break;
  case T_BYTE:
    __ st_b(val, dst);
    break;
  case T_SHORT:
    __ st_h(val, dst);
    break;
  case T_CHAR:
    __ st_h(val, dst);
    break;
  case T_INT:
    __ st_w(val, dst);
    break;
  case T_LONG:
    __ st_d(val, dst);
    break;
  case T_FLOAT:
    assert(val == noreg, "only tos");
    __ fst_s(FSF, dst);
    break;
  case T_DOUBLE:
    assert(val == noreg, "only tos");
    __ fst_d(FSF, dst);
    break;
  case T_ADDRESS:
    __ st_ptr(val, dst);
    break;
  default: Unimplemented();
  }
}

void BarrierSetAssembler::obj_equals(MacroAssembler* masm,
                                     Register obj1, Address obj2) {
  Unimplemented();
}

void BarrierSetAssembler::obj_equals(MacroAssembler* masm,
                                     Register obj1, Register obj2) {
  Unimplemented();
}

void BarrierSetAssembler::try_resolve_jobject_in_native(MacroAssembler* masm, Register jni_env,
                                                        Register obj, Register tmp, Label& slowpath) {
  __ clear_jweak_tag(obj);
  __ ld_ptr(obj, Address(obj, 0));
}

// Defines obj, preserves var_size_in_bytes, okay for t2 == var_size_in_bytes.
void BarrierSetAssembler::tlab_allocate(MacroAssembler* masm, Register obj,
                                        Register var_size_in_bytes,
                                        int con_size_in_bytes,
                                        Register t1,
                                        Register t2,
                                        Label& slow_case) {
  assert_different_registers(obj, t2);
  assert_different_registers(obj, var_size_in_bytes);
  Register end = t2;

  // verify_tlab();

  __ ld_ptr(obj, Address(TREG, JavaThread::tlab_top_offset()));
  if (var_size_in_bytes == noreg) {
    __ lea(end, Address(obj, con_size_in_bytes));
  } else {
    __ lea(end, Address(obj, var_size_in_bytes, Address::times_1, 0));
  }
  __ ld_ptr(SCR1, Address(TREG, JavaThread::tlab_end_offset()));
  __ blt_far(SCR1, end, slow_case, false);

  // update the tlab top pointer
  __ st_ptr(end, Address(TREG, JavaThread::tlab_top_offset()));

  // recover var_size_in_bytes if necessary
  if (var_size_in_bytes == end) {
    __ sub_d(var_size_in_bytes, var_size_in_bytes, obj);
  }
  // verify_tlab();
}

// Defines obj, preserves var_size_in_bytes
void BarrierSetAssembler::eden_allocate(MacroAssembler* masm, Register obj,
                                        Register var_size_in_bytes,
                                        int con_size_in_bytes,
                                        Register t1,
                                        Label& slow_case) {
  assert_different_registers(obj, var_size_in_bytes, t1);
  if (!Universe::heap()->supports_inline_contig_alloc()) {
    __ b_far(slow_case);
  } else {
    Register end = t1;
    Register heap_end = SCR2;
    Label retry;
    __ bind(retry);

    __ li(SCR1, (address)Universe::heap()->end_addr());
    __ ld_d(heap_end, SCR1, 0);

    // Get the current top of the heap
    __ li(SCR1, (address) Universe::heap()->top_addr());
    __ ll_d(obj, SCR1, 0);

    // Adjust it my the size of our new object
    if (var_size_in_bytes == noreg)
      __ addi_d(end, obj, con_size_in_bytes);
    else
      __ add_d(end, obj, var_size_in_bytes);

    // if end < obj then we wrapped around high memory
    __ blt_far(end, obj, slow_case, false);
    __ blt_far(heap_end, end, slow_case, false);

    // If heap top hasn't been changed by some other thread, update it.
    __ sc_d(end, SCR1, 0);
    __ beqz(end, retry);

    incr_allocated_bytes(masm, var_size_in_bytes, con_size_in_bytes, t1);
  }
}

void BarrierSetAssembler::incr_allocated_bytes(MacroAssembler* masm,
                                               Register var_size_in_bytes,
                                               int con_size_in_bytes,
                                               Register t1) {
  assert(t1->is_valid(), "need temp reg");

  __ ld_ptr(t1, Address(TREG, in_bytes(JavaThread::allocated_bytes_offset())));
  if (var_size_in_bytes->is_valid())
    __ add_d(t1, t1, var_size_in_bytes);
  else
    __ addi_d(t1, t1, con_size_in_bytes);
  __ st_ptr(t1, Address(TREG, in_bytes(JavaThread::allocated_bytes_offset())));
}
