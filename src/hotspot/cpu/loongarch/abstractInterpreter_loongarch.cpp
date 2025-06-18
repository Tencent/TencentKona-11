/*
 * Copyright (c) 2003, 2013, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2015, 2021, Loongson Technology. All rights reserved.
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
#include "ci/ciMethod.hpp"
#include "interpreter/interpreter.hpp"
#include "runtime/frame.inline.hpp"

// asm based interpreter deoptimization helpers
int AbstractInterpreter::size_activation(int max_stack,
                                         int temps,
                                         int extra_args,
                                         int monitors,
                                         int callee_params,
                                         int callee_locals,
                                         bool is_top_frame) {
  // Note: This calculation must exactly parallel the frame setup
  // in AbstractInterpreterGenerator::generate_method_entry.

  // fixed size of an interpreter frame:
  int overhead = frame::java_frame_sender_sp_offset -
                 frame::interpreter_frame_initial_sp_offset;
  // Our locals were accounted for by the caller (or last_frame_adjust
  // on the transistion) Since the callee parameters already account
  // for the callee's params we only need to account for the extra
  // locals.
  int size = overhead +
         (callee_locals - callee_params)*Interpreter::stackElementWords +
         monitors * frame::interpreter_frame_monitor_size() +
         temps* Interpreter::stackElementWords + extra_args;

  return size;
}

// How much stack a method activation needs in words.
int AbstractInterpreter::size_top_interpreter_activation(Method* method) {

  const int entry_size    = frame::interpreter_frame_monitor_size();

  // total overhead size: entry_size + (saved ebp thru expr stack bottom).
  // be sure to change this if you add/subtract anything to/from the overhead area
  const int overhead_size = -(frame::interpreter_frame_initial_sp_offset) + entry_size;

  const int stub_code = 6;  // see generate_call_stub
  // return overhead_size + method->max_locals() + method->max_stack() + stub_code;
  const int method_stack = (method->max_locals() + method->max_stack()) *
          Interpreter::stackElementWords;
  return overhead_size + method_stack + stub_code;
}

void AbstractInterpreter::layout_activation(Method* method,
                                           int tempcount,
                                           int popframe_extra_args,
                                           int moncount,
                                           int caller_actual_parameters,
                                           int callee_param_count,
                                           int callee_locals,
                                           frame* caller,
                                           frame* interpreter_frame,
                                           bool is_top_frame,
                                           bool is_bottom_frame) {
  // Note: This calculation must exactly parallel the frame setup
  // in AbstractInterpreterGenerator::generate_method_entry.
  // If interpreter_frame!=NULL, set up the method, locals, and monitors.
  // The frame interpreter_frame, if not NULL, is guaranteed to be the
  // right size, as determined by a previous call to this method.
  // It is also guaranteed to be walkable even though it is in a skeletal state

  // fixed size of an interpreter frame:

  int max_locals = method->max_locals() * Interpreter::stackElementWords;
  int extra_locals = (method->max_locals() - method->size_of_parameters()) * Interpreter::stackElementWords;

#ifdef ASSERT
  assert(caller->sp() == interpreter_frame->sender_sp(), "Frame not properly walkable(2)");
#endif

  interpreter_frame->interpreter_frame_set_method(method);
  // NOTE the difference in using sender_sp and interpreter_frame_sender_sp
  // interpreter_frame_sender_sp is the original sp of the caller (the unextended_sp)
  // and sender_sp is fp+8
  intptr_t* locals = interpreter_frame->sender_sp() + max_locals - 1;

#ifdef ASSERT
  if (caller->is_interpreted_frame()) {
    assert(locals < caller->fp() + frame::interpreter_frame_initial_sp_offset, "bad placement");
  }
#endif

  interpreter_frame->interpreter_frame_set_locals(locals);
  BasicObjectLock* montop = interpreter_frame->interpreter_frame_monitor_begin();
  BasicObjectLock* monbot = montop - moncount;
  interpreter_frame->interpreter_frame_set_monitor_end(montop - moncount);

  //set last sp;
  intptr_t*  esp = (intptr_t*) monbot - tempcount*Interpreter::stackElementWords -
                      popframe_extra_args;
  interpreter_frame->interpreter_frame_set_last_sp(esp);
  // All frames but the initial interpreter frame we fill in have a
  // value for sender_sp that allows walking the stack but isn't
  // truly correct. Correct the value here.
  //
  if (extra_locals != 0 &&
      interpreter_frame->sender_sp() == interpreter_frame->interpreter_frame_sender_sp() ) {
    interpreter_frame->set_interpreter_frame_sender_sp(caller->sp() + extra_locals);
  }
  *interpreter_frame->interpreter_frame_cache_addr() = method->constants()->cache();
  *interpreter_frame->interpreter_frame_mirror_addr() = method->method_holder()->java_mirror();
}

