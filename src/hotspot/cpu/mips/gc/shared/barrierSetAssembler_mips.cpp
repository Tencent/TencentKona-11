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
        __ lwu(dst, src);
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
  case T_BOOLEAN: __ lbu   (dst, src);    break;
  case T_BYTE:    __ lb    (dst, src);    break;
  case T_CHAR:    __ lhu   (dst, src);    break;
  case T_SHORT:   __ lh    (dst, src);    break;
  case T_INT:     __ lw    (dst, src);    break;
  case T_LONG:    __ ld    (dst, src);    break;
  case T_ADDRESS: __ ld_ptr(dst, src);    break;
  case T_FLOAT:
    assert(dst == noreg, "only to ftos");
    __ lwc1(FSF, src);
    break;
  case T_DOUBLE:
    assert(dst == noreg, "only to dtos");
    __ ldc1(FSF, src);
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
          __ sw(R0, dst);
        } else {
          __ sd(R0, dst);
        }
      } else {
        if (UseCompressedOops) {
          assert(!dst.uses(val), "not enough registers");
          if (is_not_null) {
            __ encode_heap_oop_not_null(val);
          } else {
            __ encode_heap_oop(val);
          }
          __ sw(val, dst);
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
    __ sb(val, dst);
    break;
  case T_BYTE:
    __ sb(val, dst);
    break;
  case T_SHORT:
    __ sh(val, dst);
    break;
  case T_CHAR:
    __ sh(val, dst);
    break;
  case T_INT:
    __ sw(val, dst);
    break;
  case T_LONG:
    __ sd(val, dst);
    break;
  case T_FLOAT:
    assert(val == noreg, "only tos");
    __ swc1(FSF, dst);
    break;
  case T_DOUBLE:
    assert(val == noreg, "only tos");
    __ sdc1(FSF, dst);
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

void BarrierSetAssembler::tlab_allocate(MacroAssembler* masm,
                                        Register thread, Register obj,
                                        Register var_size_in_bytes,
                                        int con_size_in_bytes,
                                        Register t1,
                                        Register t2,
                                        Label& slow_case) {
  Unimplemented();
}

// Defines obj, preserves var_size_in_bytes
void BarrierSetAssembler::eden_allocate(MacroAssembler* masm,
                                        Register thread, Register obj,
                                        Register var_size_in_bytes,
                                        int con_size_in_bytes,
                                        Register t1,
                                        Label& slow_case) {
  Unimplemented();
}

void BarrierSetAssembler::incr_allocated_bytes(MacroAssembler* masm, Register thread,
                                               Register var_size_in_bytes,
                                               int con_size_in_bytes,
                                               Register t1) {
  Unimplemented();
}
