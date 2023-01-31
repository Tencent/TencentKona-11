/*
 * Copyright (c) 2003, 2013, Oracle and/or its affiliates. All rights reserved.
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
#include "interpreter/bytecodeHistogram.hpp"
#include "interpreter/interp_masm.hpp"
#include "interpreter/interpreter.hpp"
#include "interpreter/interpreterRuntime.hpp"
#include "interpreter/templateInterpreterGenerator.hpp"
#include "interpreter/templateTable.hpp"
#include "oops/arrayOop.hpp"
#include "oops/methodData.hpp"
#include "oops/method.hpp"
#include "oops/oop.inline.hpp"
#include "prims/jvmtiExport.hpp"
#include "prims/jvmtiThreadState.hpp"
#include "runtime/arguments.hpp"
#include "runtime/deoptimization.hpp"
#include "runtime/frame.inline.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/stubRoutines.hpp"
#include "runtime/synchronizer.hpp"
#include "runtime/timer.hpp"
#include "runtime/vframeArray.hpp"
#include "utilities/debug.hpp"

#define __ _masm->

#define T0 RT0
#define T1 RT1
#define T2 RT2
#define T3 RT3
#define T8 RT8
#define T9 RT9

int TemplateInterpreter::InterpreterCodeSize = 500 * K;

#ifdef PRODUCT
#define BLOCK_COMMENT(str) /* nothing */
#else
#define BLOCK_COMMENT(str) __ block_comment(str)
#endif

address TemplateInterpreterGenerator::generate_slow_signature_handler() {
  address entry = __ pc();

  // Rmethod: method
  // LVP: pointer to locals
  // A3: first stack arg
  __ move(A3, SP);
  __ daddiu(SP, SP, -10 * wordSize);
  __ sd(RA, SP, 0);
  __ call_VM(noreg,
             CAST_FROM_FN_PTR(address,
                              InterpreterRuntime::slow_signature_handler),
             Rmethod, LVP, A3);

  // V0: result handler

  // Stack layout:
  //        ...
  //     10 stack arg0      <--- old sp
  //      9 float/double identifiers
  //      8 register arg7
  //        ...
  //      2 register arg1
  //      1 aligned slot
  // SP:  0 return address

  // Do FP first so we can use T3 as temp
  __ ld(T3, Address(SP, 9 * wordSize)); // float/double identifiers

  // A0 is for env.
  // If the mothed is not static, A1 will be corrected in generate_native_entry.
  for ( int i = 1; i < Argument::n_register_parameters; i++ ) {
    Register reg = as_Register(i + A0->encoding());
    FloatRegister floatreg = as_FloatRegister(i + F12->encoding());
    Label isfloatordouble, isdouble, next;

    __ andi(AT, T3, 1 << (i*2)); // Float or Double?
    __ bne(AT, R0, isfloatordouble);
    __ delayed()->nop();

    // Do Int register here
    __ ld(reg, SP, (1 + i) * wordSize);
    __ b (next);
    __ delayed()->nop();

    __ bind(isfloatordouble);
    __ andi(AT, T3, 1 << ((i*2)+1)); // Double?
    __ bne(AT, R0, isdouble);
    __ delayed()->nop();

    // Do Float Here
    __ lwc1(floatreg, SP, (1 + i) * wordSize);
    __ b(next);
    __ delayed()->nop();

    // Do Double here
    __ bind(isdouble);
    __ ldc1(floatreg, SP, (1 + i) * wordSize);

    __ bind(next);
  }

  __ ld(RA, SP, 0);
  __ daddiu(SP, SP, 10 * wordSize);
  __ jr(RA);
  __ delayed()->nop();
  return entry;
}

/**
 * Method entry for static native methods:
 *   int java.util.zip.CRC32.update(int crc, int b)
 */
address TemplateInterpreterGenerator::generate_CRC32_update_entry() {
  if (UseCRC32Intrinsics) {
    address entry = __ pc();
    Unimplemented();
    return entry;
  }
  return NULL;
}

/**
 * Method entry for static native methods:
 *   int java.util.zip.CRC32.updateBytes(int crc, byte[] b, int off, int len)
 *   int java.util.zip.CRC32.updateByteBuffer(int crc, long buf, int off, int len)
 */
address TemplateInterpreterGenerator::generate_CRC32_updateBytes_entry(AbstractInterpreter::MethodKind kind) {
  if (UseCRC32Intrinsics) {
    address entry = __ pc();
    Unimplemented();
    return entry;
  }
  return NULL;
}

/**
* Method entry for static (non-native) methods:
*   int java.util.zip.CRC32C.updateBytes(int crc, byte[] b, int off, int end)
*   int java.util.zip.CRC32C.updateDirectByteBuffer(int crc, long address, int off, int end)
*/
address TemplateInterpreterGenerator::generate_CRC32C_updateBytes_entry(AbstractInterpreter::MethodKind kind) {
  if (UseCRC32CIntrinsics) {
    address entry = __ pc();
    Unimplemented();
    return entry;
  }
  return NULL;
}

//
// Various method entries
//

address TemplateInterpreterGenerator::generate_math_entry(AbstractInterpreter::MethodKind kind) {
  if (!InlineIntrinsics) return NULL; // Generate a vanilla entry

  // These don't need a safepoint check because they aren't virtually
  // callable. We won't enter these intrinsics from compiled code.
  // If in the future we added an intrinsic which was virtually callable
  // we'd have to worry about how to safepoint so that this code is used.

  // mathematical functions inlined by compiler
  // (interpreter must provide identical implementation
  // in order to avoid monotonicity bugs when switching
  // from interpreter to compiler in the middle of some
  // computation)
  //
  // stack:
  //        [ arg ] <-- sp
  //        [ arg ]
  // retaddr in ra

  address entry_point = NULL;
  switch (kind) {
  case Interpreter::java_lang_math_abs:
    entry_point = __ pc();
    __ ldc1(F12, SP, 0);
    __ abs_d(F0, F12);
    __ move(SP, Rsender);
    break;
  case Interpreter::java_lang_math_sqrt:
    entry_point = __ pc();
    __ ldc1(F12, SP, 0);
    __ sqrt_d(F0, F12);
    __ move(SP, Rsender);
    break;
  case Interpreter::java_lang_math_sin :
  case Interpreter::java_lang_math_cos :
  case Interpreter::java_lang_math_tan :
  case Interpreter::java_lang_math_log :
  case Interpreter::java_lang_math_log10 :
  case Interpreter::java_lang_math_exp :
    entry_point = __ pc();
    __ ldc1(F12, SP, 0);
    __ move(SP, Rsender);
    __ dmtc1(RA, F24);
    __ dmtc1(SP, F25);
    __ dins(SP, R0, 0, exact_log2(StackAlignmentInBytes));
    generate_transcendental_entry(kind, 1);
    __ dmfc1(SP, F25);
    __ dmfc1(RA, F24);
    break;
  case Interpreter::java_lang_math_pow :
    entry_point = __ pc();
    __ ldc1(F12, SP, 2 * Interpreter::stackElementSize);
    __ ldc1(F13, SP, 0);
    __ move(SP, Rsender);
    __ dmtc1(RA, F24);
    __ dmtc1(SP, F25);
    __ dins(SP, R0, 0, exact_log2(StackAlignmentInBytes));
    generate_transcendental_entry(kind, 2);
    __ dmfc1(SP, F25);
    __ dmfc1(RA, F24);
    break;
  case Interpreter::java_lang_math_fmaD :
    if (UseFMA) {
      entry_point = __ pc();
      __ ldc1(F12, SP, 4 * Interpreter::stackElementSize);
      __ ldc1(F13, SP, 2 * Interpreter::stackElementSize);
      __ ldc1(F14, SP, 0);
      __ madd_d(F0, F14, F13, F12);
      __ move(SP, Rsender);
    }
    break;
  case Interpreter::java_lang_math_fmaF :
    if (UseFMA) {
      entry_point = __ pc();
      __ lwc1(F12, SP, 2 * Interpreter::stackElementSize);
      __ lwc1(F13, SP, Interpreter::stackElementSize);
      __ lwc1(F14, SP, 0);
      __ madd_s(F0, F14, F13, F12);
      __ move(SP, Rsender);
    }
    break;
  default:
    ;
  }
  if (entry_point) {
    __ jr(RA);
    __ delayed()->nop();
  }

  return entry_point;
}

  // double trigonometrics and transcendentals
  // static jdouble dsin(jdouble x);
  // static jdouble dcos(jdouble x);
  // static jdouble dtan(jdouble x);
  // static jdouble dlog(jdouble x);
  // static jdouble dlog10(jdouble x);
  // static jdouble dexp(jdouble x);
  // static jdouble dpow(jdouble x, jdouble y);

void TemplateInterpreterGenerator::generate_transcendental_entry(AbstractInterpreter::MethodKind kind, int fpargs) {
  address fn;
  switch (kind) {
  case Interpreter::java_lang_math_sin :
    if (StubRoutines::dsin() == NULL) {
      fn = CAST_FROM_FN_PTR(address, SharedRuntime::dsin);
    } else {
      fn = CAST_FROM_FN_PTR(address, StubRoutines::dsin());
    }
    break;
  case Interpreter::java_lang_math_cos :
    if (StubRoutines::dcos() == NULL) {
      fn = CAST_FROM_FN_PTR(address, SharedRuntime::dcos);
    } else {
      fn = CAST_FROM_FN_PTR(address, StubRoutines::dcos());
    }
    break;
  case Interpreter::java_lang_math_tan :
    if (StubRoutines::dtan() == NULL) {
      fn = CAST_FROM_FN_PTR(address, SharedRuntime::dtan);
    } else {
      fn = CAST_FROM_FN_PTR(address, StubRoutines::dtan());
    }
    break;
  case Interpreter::java_lang_math_log :
    if (StubRoutines::dlog() == NULL) {
      fn = CAST_FROM_FN_PTR(address, SharedRuntime::dlog);
    } else {
      fn = CAST_FROM_FN_PTR(address, StubRoutines::dlog());
    }
    break;
  case Interpreter::java_lang_math_log10 :
    if (StubRoutines::dlog10() == NULL) {
      fn = CAST_FROM_FN_PTR(address, SharedRuntime::dlog10);
    } else {
      fn = CAST_FROM_FN_PTR(address, StubRoutines::dlog10());
    }
    break;
  case Interpreter::java_lang_math_exp :
    if (StubRoutines::dexp() == NULL) {
      fn = CAST_FROM_FN_PTR(address, SharedRuntime::dexp);
    } else {
      fn = CAST_FROM_FN_PTR(address, StubRoutines::dexp());
    }
    break;
  case Interpreter::java_lang_math_pow :
    if (StubRoutines::dpow() == NULL) {
      fn = CAST_FROM_FN_PTR(address, SharedRuntime::dpow);
    } else {
      fn = CAST_FROM_FN_PTR(address, StubRoutines::dpow());
    }
    break;
  default:
    ShouldNotReachHere();
    fn = NULL;  // unreachable
  }
  __ li(T9, fn);
  __ jalr(T9);
  __ delayed()->nop();
}

// Abstract method entry
// Attempt to execute abstract method. Throw exception
address TemplateInterpreterGenerator::generate_abstract_entry(void) {

  // Rmethod: methodOop
  // V0: receiver (unused)
  // Rsender : sender 's sp
  address entry_point = __ pc();

  // abstract method entry
  // throw exception
  // adjust stack to what a normal return would do
  __ empty_expression_stack();
  __ restore_bcp();
  __ restore_locals();
  __ call_VM(noreg, CAST_FROM_FN_PTR(address, InterpreterRuntime::throw_AbstractMethodErrorWithMethod), Rmethod);
  // the call_VM checks for exception, so we should never return here.
  __ should_not_reach_here();

  return entry_point;
}


const int method_offset = frame::interpreter_frame_method_offset * wordSize;
const int bci_offset    = frame::interpreter_frame_bcp_offset    * wordSize;
const int locals_offset = frame::interpreter_frame_locals_offset * wordSize;

//-----------------------------------------------------------------------------

address TemplateInterpreterGenerator::generate_StackOverflowError_handler() {
  address entry = __ pc();

#ifdef ASSERT
  {
    Label L;
    __ addiu(T1, FP, frame::interpreter_frame_monitor_block_top_offset * wordSize);
    __ subu(T1, T1, SP); // T1 = maximal sp for current fp
    __ bgez(T1, L);     // check if frame is complete
    __ delayed()->nop();
    __ stop("interpreter frame not set up");
    __ bind(L);
  }
#endif // ASSERT
  // Restore bcp under the assumption that the current frame is still
  // interpreted
  // FIXME: please change the func restore_bcp
  // S0 is the conventional register for bcp
  __ restore_bcp();

  // expression stack must be empty before entering the VM if an
  // exception happened
  __ empty_expression_stack();
  // throw exception
  // FIXME: why do not pass parameter thread ?
  __ call_VM(NOREG, CAST_FROM_FN_PTR(address, InterpreterRuntime::throw_StackOverflowError));
  return entry;
}

address TemplateInterpreterGenerator::generate_ArrayIndexOutOfBounds_handler() {
  address entry = __ pc();
  // expression stack must be empty before entering the VM if an
  // exception happened
  __ empty_expression_stack();
  // ??? convention: expect array in register A1
  __ call_VM(noreg, CAST_FROM_FN_PTR(address,
  InterpreterRuntime::throw_ArrayIndexOutOfBoundsException), A1, A2);
  return entry;
}

address TemplateInterpreterGenerator::generate_ClassCastException_handler() {
  address entry = __ pc();

  // expression stack must be empty before entering the VM if an
  // exception happened
  __ empty_expression_stack();
  __ empty_FPU_stack();
  __ call_VM(noreg, CAST_FROM_FN_PTR(address, InterpreterRuntime::throw_ClassCastException),  FSR);
  return entry;
}

address TemplateInterpreterGenerator::generate_exception_handler_common(
        const char* name, const char* message, bool pass_oop) {
  assert(!pass_oop || message == NULL, "either oop or message but not both");
  address entry = __ pc();

  // expression stack must be empty before entering the VM if an exception happened
  __ empty_expression_stack();
  // setup parameters
  __ li(A1, (long)name);
  if (pass_oop) {
    __ call_VM(V0,
    CAST_FROM_FN_PTR(address, InterpreterRuntime::create_klass_exception), A1, FSR);
  } else {
    __ li(A2, (long)message);
    __ call_VM(V0,
    CAST_FROM_FN_PTR(address, InterpreterRuntime::create_exception), A1, A2);
  }
  // throw exception
  __ jmp(Interpreter::throw_exception_entry(), relocInfo::none);
  __ delayed()->nop();
  return entry;
}

address TemplateInterpreterGenerator::generate_return_entry_for(TosState state, int step, size_t index_size) {

  address entry = __ pc();

  // Restore stack bottom in case i2c adjusted stack
  __ ld(SP, Address(FP, frame::interpreter_frame_last_sp_offset * wordSize));
  // and NULL it as marker that sp is now tos until next java call
  __ sd(R0, FP, frame::interpreter_frame_last_sp_offset * wordSize);

  __ restore_bcp();
  __ restore_locals();

  // mdp: T8
  // ret: FSR
  // tmp: T9
  if (state == atos) {
    Register mdp = T8;
    Register tmp = T9;
    __ profile_return_type(mdp, FSR, tmp);
  }


  const Register cache = T9;
  const Register index = T3;
  __ get_cache_and_index_at_bcp(cache, index, 1, index_size);

  const Register flags = cache;
  __ dsll(AT, index, Address::times_ptr);
  __ daddu(AT, cache, AT);
  __ lw(flags, AT, in_bytes(ConstantPoolCache::base_offset() + ConstantPoolCacheEntry::flags_offset()));
  __ andi(flags, flags, ConstantPoolCacheEntry::parameter_size_mask);
  __ dsll(AT, flags, Interpreter::logStackElementSize);
  __ daddu(SP, SP, AT);

  Register java_thread;
#ifndef OPT_THREAD
    java_thread = T9;
    __ get_thread(java_thread);
#else
    java_thread = TREG;
#endif

  __ check_and_handle_popframe(java_thread);
  __ check_and_handle_earlyret(java_thread);

  __ dispatch_next(state, step);

  return entry;
}


address TemplateInterpreterGenerator::generate_deopt_entry_for(TosState state,
                                                               int step,
                                                               address continuation) {
  address entry = __ pc();
  // NULL last_sp until next java call
  __ sd(R0, FP, frame::interpreter_frame_last_sp_offset * wordSize);
  __ restore_bcp();
  __ restore_locals();
  // handle exceptions
  {
    Label L;
    const Register thread = TREG;
#ifndef OPT_THREAD
    __ get_thread(thread);
#endif
    __ ld(AT, thread, in_bytes(Thread::pending_exception_offset()));
    __ beq(AT, R0, L);
    __ delayed()->nop();
    __ call_VM(noreg, CAST_FROM_FN_PTR(address, InterpreterRuntime::throw_pending_exception));
    __ should_not_reach_here();
    __ bind(L);
  }
  if (continuation == NULL) {
    __ dispatch_next(state, step);
  } else {
    __ jump_to_entry(continuation);
    __ delayed()->nop();
  }
  return entry;
}

int AbstractInterpreter::BasicType_as_index(BasicType type) {
  int i = 0;
  switch (type) {
    case T_BOOLEAN: i = 0; break;
    case T_CHAR   : i = 1; break;
    case T_BYTE   : i = 2; break;
    case T_SHORT  : i = 3; break;
    case T_INT    : // fall through
    case T_LONG   : // fall through
    case T_VOID   : i = 4; break;
    case T_FLOAT  : i = 5; break;
    case T_DOUBLE : i = 6; break;
    case T_OBJECT : // fall through
    case T_ARRAY  : i = 7; break;
    default       : ShouldNotReachHere();
  }
  assert(0 <= i && i < AbstractInterpreter::number_of_result_handlers,
         "index out of bounds");
  return i;
}


address TemplateInterpreterGenerator::generate_result_handler_for(
        BasicType type) {
  address entry = __ pc();
  switch (type) {
    case T_BOOLEAN: __ c2bool(V0);             break;
    case T_CHAR   : __ andi(V0, V0, 0xFFFF);   break;
    case T_BYTE   : __ sign_extend_byte (V0);  break;
    case T_SHORT  : __ sign_extend_short(V0);  break;
    case T_INT    : /* nothing to do */        break;
    case T_FLOAT  : /* nothing to do */        break;
    case T_DOUBLE : /* nothing to do */        break;
    case T_OBJECT :
    {
       __ ld(V0, FP, frame::interpreter_frame_oop_temp_offset * wordSize);
      __ verify_oop(V0);         // and verify it
    }
                 break;
    default       : ShouldNotReachHere();
  }
  __ jr(RA);                                  // return from result handler
  __ delayed()->nop();
  return entry;
}

address TemplateInterpreterGenerator::generate_safept_entry_for(
        TosState state,
        address runtime_entry) {
  address entry = __ pc();
  __ push(state);
  __ call_VM(noreg, runtime_entry);
  __ dispatch_via(vtos, Interpreter::_normal_table.table_for(vtos));
  return entry;
}



// Helpers for commoning out cases in the various type of method entries.
//


// increment invocation count & check for overflow
//
// Note: checking for negative value instead of overflow
//       so we have a 'sticky' overflow test
//
// prerequisites : method in T0, invocation counter in T3
void TemplateInterpreterGenerator::generate_counter_incr(
        Label* overflow,
        Label* profile_method,
        Label* profile_method_continue) {
  Label done;
  const Address invocation_counter(FSR, in_bytes(MethodCounters::invocation_counter_offset())
      + in_bytes(InvocationCounter::counter_offset()));
  const Address backedge_counter  (FSR, in_bytes(MethodCounters::backedge_counter_offset())
      + in_bytes(InvocationCounter::counter_offset()));

  __ get_method_counters(Rmethod, FSR, done);

  if (ProfileInterpreter) { // %%% Merge this into methodDataOop
    __ lw(T9, FSR, in_bytes(MethodCounters::interpreter_invocation_counter_offset()));
    __ incrementl(T9, 1);
    __ sw(T9, FSR, in_bytes(MethodCounters::interpreter_invocation_counter_offset()));
  }
  // Update standard invocation counters
  __ lw(T3, invocation_counter);
  __ increment(T3, InvocationCounter::count_increment);
  __ sw(T3, invocation_counter);  // save invocation count

  __ lw(FSR, backedge_counter);  // load backedge counter
  __ li(AT, InvocationCounter::count_mask_value);   // mask out the status bits
  __ andr(FSR, FSR, AT);

  __ daddu(T3, T3, FSR);          // add both counters

  if (ProfileInterpreter && profile_method != NULL) {
    // Test to see if we should create a method data oop
    if (Assembler::is_simm16(InvocationCounter::InterpreterProfileLimit)) {
      __ slti(AT, T3, InvocationCounter::InterpreterProfileLimit);
    } else {
      __ li(AT, (long)&InvocationCounter::InterpreterProfileLimit);
      __ lw(AT, AT, 0);
      __ slt(AT, T3, AT);
    }

    __ bne_far(AT, R0, *profile_method_continue);
    __ delayed()->nop();

    // if no method data exists, go to profile_method
    __ test_method_data_pointer(FSR, *profile_method);
  }

  if (Assembler::is_simm16(CompileThreshold)) {
    __ srl(AT, T3, InvocationCounter::count_shift);
    __ slti(AT, AT, CompileThreshold);
  } else {
    __ li(AT, (long)&InvocationCounter::InterpreterInvocationLimit);
    __ lw(AT, AT, 0);
    __ slt(AT, T3, AT);
  }

  __ beq_far(AT, R0, *overflow);
  __ delayed()->nop();
  __ bind(done);
}

void TemplateInterpreterGenerator::generate_counter_overflow(Label& do_continue) {

  // Asm interpreter on entry
  // S7 - locals
  // S0 - bcp
  // Rmethod - method
  // FP - interpreter frame

  // On return (i.e. jump to entry_point)
  // Rmethod - method
  // RA - return address of interpreter caller
  // tos - the last parameter to Java method
  // SP - sender_sp

  // the bcp is valid if and only if it's not null
  __ call_VM(NOREG, CAST_FROM_FN_PTR(address,
      InterpreterRuntime::frequency_counter_overflow), R0);
  __ ld(Rmethod, FP, method_offset);
  // Preserve invariant that S0/S7 contain bcp/locals of sender frame
  __ b_far(do_continue);
  __ delayed()->nop();
}

// See if we've got enough room on the stack for locals plus overhead.
// The expression stack grows down incrementally, so the normal guard
// page mechanism will work for that.
//
// NOTE: Since the additional locals are also always pushed (wasn't
// obvious in generate_method_entry) so the guard should work for them
// too.
//
// Args:
//      T2: number of additional locals this frame needs (what we must check)
//      T0: Method*
//
void TemplateInterpreterGenerator::generate_stack_overflow_check(void) {
  // see if we've got enough room on the stack for locals plus overhead.
  // the expression stack grows down incrementally, so the normal guard
  // page mechanism will work for that.
  //
  // Registers live on entry:
  //
  // T0: Method*
  // T2: number of additional locals this frame needs (what we must check)

  // NOTE:  since the additional locals are also always pushed (wasn't obvious in
  // generate_method_entry) so the guard should work for them too.
  //

  const int entry_size    = frame::interpreter_frame_monitor_size() * wordSize;

  // total overhead size: entry_size + (saved fp thru expr stack bottom).
  // be sure to change this if you add/subtract anything to/from the overhead area
  const int overhead_size = -(frame::interpreter_frame_initial_sp_offset*wordSize)
    + entry_size;

  const int page_size = os::vm_page_size();

  Label after_frame_check;

  // see if the frame is greater than one page in size. If so,
  // then we need to verify there is enough stack space remaining
  // for the additional locals.
  __ move(AT, (page_size - overhead_size) / Interpreter::stackElementSize);
  __ slt(AT, AT, T2);
  __ beq(AT, R0, after_frame_check);
  __ delayed()->nop();

  // compute sp as if this were going to be the last frame on
  // the stack before the red zone
#ifndef OPT_THREAD
  Register thread = T1;
  __ get_thread(thread);
#else
  Register thread = TREG;
#endif

  // locals + overhead, in bytes
  __ dsll(T3, T2, Interpreter::logStackElementSize);
  __ daddiu(T3, T3, overhead_size);   // locals * 4 + overhead_size --> T3

#ifdef ASSERT
  Label stack_base_okay, stack_size_okay;
  // verify that thread stack base is non-zero
  __ ld(AT, thread, in_bytes(Thread::stack_base_offset()));
  __ bne(AT, R0, stack_base_okay);
  __ delayed()->nop();
  __ stop("stack base is zero");
  __ bind(stack_base_okay);
  // verify that thread stack size is non-zero
  __ ld(AT, thread, in_bytes(Thread::stack_size_offset()));
  __ bne(AT, R0, stack_size_okay);
  __ delayed()->nop();
  __ stop("stack size is zero");
  __ bind(stack_size_okay);
#endif

  // Add stack base to locals and subtract stack size
  __ ld(AT, thread, in_bytes(Thread::stack_base_offset())); // stack_base --> AT
  __ daddu(T3, T3, AT);   // locals * 4 + overhead_size + stack_base--> T3
  __ ld(AT, thread, in_bytes(Thread::stack_size_offset()));  // stack_size --> AT
  __ dsubu(T3, T3, AT);  // locals * 4 + overhead_size + stack_base - stack_size --> T3

  // Use the bigger size for banging.
  const int max_bang_size = (int)MAX2(JavaThread::stack_shadow_zone_size(), JavaThread::stack_guard_zone_size());

  // add in the redzone and yellow size
  __ move(AT, max_bang_size);
  __ addu(T3, T3, AT);

  // check against the current stack bottom
  __ slt(AT, T3, SP);
  __ bne(AT, R0, after_frame_check);
  __ delayed()->nop();

  // Note: the restored frame is not necessarily interpreted.
  // Use the shared runtime version of the StackOverflowError.
  __ move(SP, Rsender);
  assert(StubRoutines::throw_StackOverflowError_entry() != NULL, "stub not yet generated");
  __ jmp(StubRoutines::throw_StackOverflowError_entry(), relocInfo::runtime_call_type);
  __ delayed()->nop();

  // all done with frame size check
  __ bind(after_frame_check);
}

// Allocate monitor and lock method (asm interpreter)
// Rmethod - Method*
void TemplateInterpreterGenerator::lock_method(void) {
  // synchronize method
  const int entry_size = frame::interpreter_frame_monitor_size() * wordSize;

#ifdef ASSERT
  { Label L;
    __ lw(T0, Rmethod, in_bytes(Method::access_flags_offset()));
    __ andi(T0, T0, JVM_ACC_SYNCHRONIZED);
    __ bne(T0, R0, L);
    __ delayed()->nop();
    __ stop("method doesn't need synchronization");
    __ bind(L);
  }
#endif // ASSERT
  // get synchronization object
  {
    Label done;
    __ lw(T0, Rmethod, in_bytes(Method::access_flags_offset()));
    __ andi(T2, T0, JVM_ACC_STATIC);
    __ ld(T0, LVP, Interpreter::local_offset_in_bytes(0));
    __ beq(T2, R0, done);
    __ delayed()->nop();
    __ load_mirror(T0, Rmethod, T9);
    __ bind(done);
  }
  // add space for monitor & lock
  __ daddiu(SP, SP, (-1) * entry_size);           // add space for a monitor entry
  __ sd(SP, FP, frame::interpreter_frame_monitor_block_top_offset * wordSize);
  // set new monitor block top
  __ sd(T0, SP, BasicObjectLock::obj_offset_in_bytes());   // store object
  // FIXME: I do not know what lock_object will do and what it will need
  __ move(c_rarg0, SP);      // object address
  __ lock_object(c_rarg0);
}

// Generate a fixed interpreter frame. This is identical setup for
// interpreted methods and for native methods hence the shared code.
void TemplateInterpreterGenerator::generate_fixed_frame(bool native_call) {

  // [ local var m-1      ] <--- sp
  //   ...
  // [ local var 0        ]
  // [ argumnet word n-1  ] <--- T0(sender's sp)
  //   ...
  // [ argument word 0    ] <--- S7

  // initialize fixed part of activation frame
  // sender's sp in Rsender
  int i = 0;
  int frame_size = 10;
#ifndef CORE
  ++frame_size;
#endif
  __ daddiu(SP, SP, (-frame_size) * wordSize);
  __ sd(RA, SP, (frame_size - 1) * wordSize);   // save return address
  __ sd(FP, SP, (frame_size - 2) * wordSize);  // save sender's fp
  __ daddiu(FP, SP, (frame_size - 2) * wordSize);
  __ sd(Rsender, FP, (-++i) * wordSize);  // save sender's sp
  __ sd(R0, FP,(-++i) * wordSize);       //save last_sp as null
  __ sd(LVP, FP, (-++i) * wordSize);  // save locals offset
  __ ld(BCP, Rmethod, in_bytes(Method::const_offset())); // get constMethodOop
  __ daddiu(BCP, BCP, in_bytes(ConstMethod::codes_offset())); // get codebase
  __ sd(Rmethod, FP, (-++i) * wordSize);                              // save Method*
  // Get mirror and store it in the frame as GC root for this Method*
  __ load_mirror(T2, Rmethod, T9);
  __ sd(T2, FP, (-++i) * wordSize); // Mirror
#ifndef CORE
  if (ProfileInterpreter) {
    Label method_data_continue;
    __ ld(AT, Rmethod,  in_bytes(Method::method_data_offset()));
    __ beq(AT, R0, method_data_continue);
    __ delayed()->nop();
    __ daddiu(AT, AT, in_bytes(MethodData::data_offset()));
    __ bind(method_data_continue);
    __ sd(AT, FP,  (-++i) * wordSize);
  } else {
    __ sd(R0, FP, (-++i) * wordSize);
  }
#endif // !CORE

  __ ld(T2, Rmethod, in_bytes(Method::const_offset()));
  __ ld(T2, T2, in_bytes(ConstMethod::constants_offset()));
  __ ld(T2, T2, ConstantPool::cache_offset_in_bytes());
  __ sd(T2, FP, (-++i) * wordSize);                    // set constant pool cache
  if (native_call) {
    __ sd(R0, FP, (-++i) * wordSize);          // no bcp
  } else {
    __ sd(BCP, FP, (-++i) * wordSize);          // set bcp
  }
  __ sd(SP, FP, (-++i) * wordSize);               // reserve word for pointer to expression stack bottom
  assert(i + 2 == frame_size, "i + 2 should be equal to frame_size");
}

// End of helpers

// Various method entries
//------------------------------------------------------------------------------------------------------------------------
//
//

// Method entry for java.lang.ref.Reference.get.
address TemplateInterpreterGenerator::generate_Reference_get_entry(void) {
  address entry = __ pc();
  Label slow_path;
  __ b(slow_path);
  __ delayed()->nop();

  // generate a vanilla interpreter entry as the slow path
  __ bind(slow_path);
  __ jump_to_entry(Interpreter::entry_for_kind(Interpreter::zerolocals));
  __ delayed()->nop();
  return entry;
}

// Interpreter stub for calling a native method. (asm interpreter)
// This sets up a somewhat different looking stack for calling the
// native method than the typical interpreter frame setup.
address TemplateInterpreterGenerator::generate_native_entry(bool synchronized) {
  // determine code generation flags
  bool inc_counter  = UseCompiler || CountCompiledCalls || LogTouchedMethods;
  // Rsender: sender's sp
  // Rmethod: Method*
  address entry_point = __ pc();

#ifndef CORE
  const Address invocation_counter(Rmethod,in_bytes(MethodCounters::invocation_counter_offset() +
  InvocationCounter::counter_offset()));
#endif

  // get parameter size (always needed)
  // the size in the java stack
  __ ld(V0, Rmethod, in_bytes(Method::const_offset()));
  __ lhu(V0, V0, in_bytes(ConstMethod::size_of_parameters_offset()));

  // native calls don't need the stack size check since they have no expression stack
  // and the arguments are already on the stack and we only add a handful of words
  // to the stack

  // Rmethod: Method*
  // V0: size of parameters
  // Layout of frame at this point
  //
  // [ argument word n-1  ] <--- sp
  //   ...
  // [ argument word 0    ]

  // for natives the size of locals is zero

  // compute beginning of parameters (S7)
  __ dsll(LVP, V0, Address::times_8);
  __ daddiu(LVP, LVP, (-1) * wordSize);
  __ daddu(LVP, LVP, SP);


  // add 2 zero-initialized slots for native calls
  // 1 slot for native oop temp offset (setup via runtime)
  // 1 slot for static native result handler3 (setup via runtime)
  __ push2(R0, R0);

  // Layout of frame at this point
  // [ method holder mirror  ] <--- sp
  // [ result type info      ]
  // [ argument word n-1     ] <--- T0
  //   ...
  // [ argument word 0      ] <--- LVP


#ifndef CORE
  if (inc_counter) __ lw(T3, invocation_counter);  // (pre-)fetch invocation count
#endif

  // initialize fixed part of activation frame
  generate_fixed_frame(true);
  // after this function, the layout of frame is as following
  //
  // [ monitor block top        ] <--- sp ( the top monitor entry )
  // [ byte code pointer (0)    ] (if native, bcp = 0)
  // [ constant pool cache      ]
  // [ Mirror                   ]
  // [ Method*                  ]
  // [ locals offset            ]
  // [ sender's sp              ]
  // [ sender's fp              ]
  // [ return address           ] <--- fp
  // [ method holder mirror     ]
  // [ result type info         ]
  // [ argumnet word n-1        ] <--- sender's sp
  //   ...
  // [ argument word 0          ] <--- S7


  // make sure method is native & not abstract
#ifdef ASSERT
  __ lw(T0, Rmethod, in_bytes(Method::access_flags_offset()));
  {
    Label L;
    __ andi(AT, T0, JVM_ACC_NATIVE);
    __ bne(AT, R0, L);
    __ delayed()->nop();
    __ stop("tried to execute native method as non-native");
    __ bind(L);
  }
  {
    Label L;
    __ andi(AT, T0, JVM_ACC_ABSTRACT);
    __ beq(AT, R0, L);
    __ delayed()->nop();
    __ stop("tried to execute abstract method in interpreter");
    __ bind(L);
  }
#endif

  // Since at this point in the method invocation the exception handler
  // would try to exit the monitor of synchronized methods which hasn't
  // been entered yet, we set the thread local variable
  // _do_not_unlock_if_synchronized to true. The remove_activation will
  // check this flag.
  Register thread = TREG;
#ifndef OPT_THREAD
  __ get_thread(thread);
#endif
  __ move(AT, (int)true);
  __ sb(AT, thread, in_bytes(JavaThread::do_not_unlock_if_synchronized_offset()));

#ifndef CORE
  // increment invocation count & check for overflow
  Label invocation_counter_overflow;
  if (inc_counter) {
    generate_counter_incr(&invocation_counter_overflow, NULL, NULL);
  }

  Label continue_after_compile;
  __ bind(continue_after_compile);
#endif // CORE

  bang_stack_shadow_pages(true);

  // reset the _do_not_unlock_if_synchronized flag
#ifndef OPT_THREAD
  __ get_thread(thread);
#endif
  __ sb(R0, thread, in_bytes(JavaThread::do_not_unlock_if_synchronized_offset()));

  // check for synchronized methods
  // Must happen AFTER invocation_counter check and stack overflow check,
  // so method is not locked if overflows.
  if (synchronized) {
    lock_method();
  } else {
    // no synchronization necessary
#ifdef ASSERT
    {
      Label L;
      __ lw(T0, Rmethod, in_bytes(Method::access_flags_offset()));
      __ andi(AT, T0, JVM_ACC_SYNCHRONIZED);
      __ beq(AT, R0, L);
      __ delayed()->nop();
      __ stop("method needs synchronization");
      __ bind(L);
    }
#endif
  }

  // after method_lock, the layout of frame is as following
  //
  // [ monitor entry            ] <--- sp
  //   ...
  // [ monitor entry            ]
  // [ monitor block top        ] ( the top monitor entry )
  // [ byte code pointer (0)    ] (if native, bcp = 0)
  // [ constant pool cache      ]
  // [ Mirror                   ]
  // [ Method*                  ]
  // [ locals offset            ]
  // [ sender's sp              ]
  // [ sender's fp              ]
  // [ return address           ] <--- fp
  // [ method holder mirror     ]
  // [ result type info         ]
  // [ argumnet word n-1        ] <--- ( sender's sp )
  //   ...
  // [ argument word 0          ] <--- S7

  // start execution
#ifdef ASSERT
  {
    Label L;
    __ ld(AT, FP, frame::interpreter_frame_monitor_block_top_offset * wordSize);
    __ beq(AT, SP, L);
    __ delayed()->nop();
    __ stop("broken stack frame setup in interpreter in asm");
    __ bind(L);
  }
#endif

  // jvmti/jvmpi support
  __ notify_method_entry();

  // work registers
  const Register method = Rmethod;
  const Register t      = T8;

  __ get_method(method);
  {
    Label L, Lstatic;
    __ ld(t,method,in_bytes(Method::const_offset()));
    __ lhu(t, t, in_bytes(ConstMethod::size_of_parameters_offset()));
    // MIPS n64 ABI: caller does not reserve space for the register auguments.
    // A0 and A1(if needed)
    __ lw(AT, Rmethod, in_bytes(Method::access_flags_offset()));
    __ andi(AT, AT, JVM_ACC_STATIC);
    __ beq(AT, R0, Lstatic);
    __ delayed()->nop();
    __ daddiu(t, t, 1);
    __ bind(Lstatic);
    __ daddiu(t, t, -7);
    __ blez(t, L);
    __ delayed()->nop();
    __ dsll(t, t, Address::times_8);
    __ dsubu(SP, SP, t);
    __ bind(L);
  }
  __ move(AT, -(StackAlignmentInBytes));
  __ andr(SP, SP, AT);
  __ move(AT, SP);
  // [        ] <--- sp
  //   ...                        (size of parameters - 8 )
  // [ monitor entry            ]
  //   ...
  // [ monitor entry            ]
  // [ monitor block top        ] ( the top monitor entry )
  // [ byte code pointer (0)    ] (if native, bcp = 0)
  // [ constant pool cache      ]
  // [ Mirror                   ]
  // [ Method*                  ]
  // [ locals offset            ]
  // [ sender's sp              ]
  // [ sender's fp              ]
  // [ return address           ] <--- fp
  // [ method holder mirror     ]
  // [ result type info         ]
  // [ argumnet word n-1        ] <--- ( sender's sp )
  //   ...
  // [ argument word 0          ] <--- LVP

  // get signature handler
  {
    Label L;
    __ ld(T9, method, in_bytes(Method::signature_handler_offset()));
    __ bne(T9, R0, L);
    __ delayed()->nop();
    __ call_VM(NOREG, CAST_FROM_FN_PTR(address,
               InterpreterRuntime::prepare_native_call), method);
    __ get_method(method);
    __ ld(T9, method, in_bytes(Method::signature_handler_offset()));
    __ bind(L);
  }

  // call signature handler
  // FIXME: when change codes in InterpreterRuntime, note this point
  // from: begin of parameters
  assert(InterpreterRuntime::SignatureHandlerGenerator::from() == LVP, "adjust this code");
  // to: current sp
  assert(InterpreterRuntime::SignatureHandlerGenerator::to  () == SP, "adjust this code");
  // temp: T3
  assert(InterpreterRuntime::SignatureHandlerGenerator::temp() == t  , "adjust this code");

  __ jalr(T9);
  __ delayed()->nop();
  __ get_method(method);

  //
  // if native function is static, and its second parameter has type length of double word,
  // and first parameter has type length of word, we have to reserve one word
  // for the first parameter, according to mips o32 abi.
  // if native function is not static, and its third parameter has type length of double word,
  // and second parameter has type length of word, we have to reserve one word for the second
  // parameter.
  //


  // result handler is in V0
  // set result handler
  __ sd(V0, FP, (frame::interpreter_frame_result_handler_offset)*wordSize);

#define FIRSTPARA_SHIFT_COUNT 5
#define SECONDPARA_SHIFT_COUNT 9
#define THIRDPARA_SHIFT_COUNT 13
#define PARA_MASK  0xf

  // pass mirror handle if static call
  {
    Label L;
    __ lw(t, method, in_bytes(Method::access_flags_offset()));
    __ andi(AT, t, JVM_ACC_STATIC);
    __ beq(AT, R0, L);
    __ delayed()->nop();

    // get mirror
    __ load_mirror(t, method, T9);
    // copy mirror into activation frame
    __ sd(t, FP, frame::interpreter_frame_oop_temp_offset * wordSize);
    // pass handle to mirror
    __ daddiu(t, FP, frame::interpreter_frame_oop_temp_offset * wordSize);
    __ move(A1, t);
    __ bind(L);
  }

  // [ mthd holder mirror ptr   ] <--- sp  --------------------| (only for static method)
  // [                          ]                              |
  //   ...                        size of parameters(or +1)    |
  // [ monitor entry            ]                              |
  //   ...                                                     |
  // [ monitor entry            ]                              |
  // [ monitor block top        ] ( the top monitor entry )    |
  // [ byte code pointer (0)    ] (if native, bcp = 0)         |
  // [ constant pool cache      ]                              |
  // [ Mirror                   ]                              |
  // [ Method*                  ]                              |
  // [ locals offset            ]                              |
  // [ sender's sp              ]                              |
  // [ sender's fp              ]                              |
  // [ return address           ] <--- fp                      |
  // [ method holder mirror     ] <----------------------------|
  // [ result type info         ]
  // [ argumnet word n-1        ] <--- ( sender's sp )
  //   ...
  // [ argument word 0          ] <--- S7

  // get native function entry point
  { Label L;
    __ ld(T9, method, in_bytes(Method::native_function_offset()));
    __ li(V1, SharedRuntime::native_method_throw_unsatisfied_link_error_entry());
    __ bne(V1, T9, L);
    __ delayed()->nop();
    __ call_VM(noreg, CAST_FROM_FN_PTR(address, InterpreterRuntime::prepare_native_call), method);
    __ get_method(method);
    __ ld(T9, method, in_bytes(Method::native_function_offset()));
    __ bind(L);
  }

  // pass JNIEnv
  // native function in T9
#ifndef OPT_THREAD
  __ get_thread(thread);
#endif
  __ daddiu(t, thread, in_bytes(JavaThread::jni_environment_offset()));
  __ move(A0, t);
  // [ jni environment          ] <--- sp
  // [ mthd holder mirror ptr   ] ---------------------------->| (only for static method)
  // [                          ]                              |
  //   ...                        size of parameters           |
  // [ monitor entry            ]                              |
  //   ...                                                     |
  // [ monitor entry            ]                              |
  // [ monitor block top        ] ( the top monitor entry )    |
  // [ byte code pointer (0)    ] (if native, bcp = 0)         |
  // [ constant pool cache      ]                              |
  // [ Mirror                   ]                              |
  // [ Method*                  ]                              |
  // [ locals offset            ]                              |
  // [ sender's sp              ]                              |
  // [ sender's fp              ]                              |
  // [ return address           ] <--- fp                      |
  // [ method holder mirror     ] <----------------------------|
  // [ result type info         ]
  // [ argumnet word n-1        ] <--- ( sender's sp )
  //   ...
  // [ argument word 0          ] <--- S7

  // set_last_Java_frame_before_call
  __ sd(FP, thread, in_bytes(JavaThread::last_Java_fp_offset()));
  // Change state to native (we save the return address in the thread, since it might not
  // be pushed on the stack when we do a a stack traversal). It is enough that the pc()
  // points into the right code segment. It does not have to be the correct return pc.
  __ li(t, __ pc());
  __ sd(t, thread, in_bytes(JavaThread::last_Java_pc_offset()));
  __ sd(SP, thread, in_bytes(JavaThread::last_Java_sp_offset()));

  // change thread state
#ifdef ASSERT
  {
    Label L;
    __ lw(t, thread, in_bytes(JavaThread::thread_state_offset()));
    __ daddiu(t, t, (-1) * _thread_in_Java);
    __ beq(t, R0, L);
    __ delayed()->nop();
    __ stop("Wrong thread state in native stub");
    __ bind(L);
  }
#endif

  __ move(t, _thread_in_native);
  if(os::is_MP()) {
    __ sync(); // store release
  }
  __ sw(t, thread, in_bytes(JavaThread::thread_state_offset()));

  // call native method
  __ jalr(T9);
  __ delayed()->nop();
  // result potentially in V0 or F0


  // via _last_native_pc and not via _last_jave_sp
  // NOTE: the order of theses push(es) is known to frame::interpreter_frame_result.
  //  If the order changes or anything else is added to the stack the code in
  // interpreter_frame_result will have to be changed.
  //FIXME, should modify here
  // save return value to keep the value from being destroyed by other calls
  __ push(dtos);
  __ push(ltos);

  // change thread state
#ifndef OPT_THREAD
  __ get_thread(thread);
#endif
  __ move(t, _thread_in_native_trans);
  if(os::is_MP()) {
    __ sync(); // store release
  }
  __ sw(t, thread, in_bytes(JavaThread::thread_state_offset()));

  if(os::is_MP()) {
    if (UseMembar) {
      // Force this write out before the read below
      __ sync();
    } else {
      // Write serialization page so VM thread can do a pseudo remote membar.
      // We use the current thread pointer to calculate a thread specific
      // offset to write to within the page. This minimizes bus traffic
      // due to cache line collision.
      __ serialize_memory(thread, A0);
    }
  }

  // check for safepoint operation in progress and/or pending suspend requests
  { Label Continue;

    // Don't use call_VM as it will see a possible pending exception and forward it
    // and never return here preventing us from clearing _last_native_pc down below.
    // Also can't use call_VM_leaf either as it will check to see if BCP & LVP are
    // preserved and correspond to the bcp/locals pointers. So we do a runtime call
    // by hand.
    //
    Label slow_path;

    __ safepoint_poll_acquire(slow_path, thread);
    __ lw(AT, thread, in_bytes(JavaThread::suspend_flags_offset()));
    __ beq(AT, R0, Continue);
    __ delayed()->nop();
    __ bind(slow_path);
    __ move(A0, thread);
    __ call(CAST_FROM_FN_PTR(address, JavaThread::check_special_condition_for_native_trans),
                             relocInfo::runtime_call_type);
    __ delayed()->nop();

#ifndef OPT_THREAD
    __ get_thread(thread);
#endif
    //add for compressedoops
    __ reinit_heapbase();
    __ bind(Continue);
  }

  // change thread state
  __ move(t, _thread_in_Java);
  if(os::is_MP()) {
    __ sync(); // store release
  }
  __ sw(t, thread, in_bytes(JavaThread::thread_state_offset()));
  __ reset_last_Java_frame(thread, true);

  if (CheckJNICalls) {
    // clear_pending_jni_exception_check
    __ sd(R0, thread, in_bytes(JavaThread::pending_jni_exception_check_fn_offset()));
  }

  // reset handle block
  __ ld(t, thread, in_bytes(JavaThread::active_handles_offset()));
  __ sw(R0, t, JNIHandleBlock::top_offset_in_bytes());

  // If result was an oop then unbox and save it in the frame
  {
    Label no_oop;
    //FIXME, addi only support 16-bit imeditate
    __ ld(AT, FP, frame::interpreter_frame_result_handler_offset*wordSize);
    __ li(T0, AbstractInterpreter::result_handler(T_OBJECT));
    __ bne(AT, T0, no_oop);
    __ delayed()->nop();
    __ pop(ltos);
    // Unbox oop result, e.g. JNIHandles::resolve value.
    __ resolve_jobject(V0, thread, T9);
    __ sd(V0, FP, (frame::interpreter_frame_oop_temp_offset)*wordSize);
    // keep stack depth as expected by pushing oop which will eventually be discarded
    __ push(ltos);
    __ bind(no_oop);
  }
  {
    Label no_reguard;
    __ lw(t, thread, in_bytes(JavaThread::stack_guard_state_offset()));
    __ move(AT, (u1)JavaThread::stack_guard_yellow_reserved_disabled);
    __ bne(t, AT, no_reguard);
    __ delayed()->nop();
    __ pushad();
    __ move(S5_heapbase, SP);
    __ move(AT, -StackAlignmentInBytes);
    __ andr(SP, SP, AT);
    __ call(CAST_FROM_FN_PTR(address, SharedRuntime::reguard_yellow_pages), relocInfo::runtime_call_type);
    __ delayed()->nop();
    __ move(SP, S5_heapbase);
    __ popad();
    //add for compressedoops
    __ reinit_heapbase();
    __ bind(no_reguard);
  }
  // restore BCP to have legal interpreter frame,
  // i.e., bci == 0 <=> BCP == code_base()
  // Can't call_VM until bcp is within reasonable.
  __ get_method(method);      // method is junk from thread_in_native to now.
  __ ld(BCP, method, in_bytes(Method::const_offset()));
  __ lea(BCP, Address(BCP, in_bytes(ConstMethod::codes_offset())));
  // handle exceptions (exception handling will handle unlocking!)
  {
    Label L;
    __ ld(t, thread, in_bytes(Thread::pending_exception_offset()));
    __ beq(t, R0, L);
    __ delayed()->nop();
    // Note: At some point we may want to unify this with the code used in
    // call_VM_base();
    // i.e., we should use the StubRoutines::forward_exception code. For now this
    // doesn't work here because the sp is not correctly set at this point.
    __ MacroAssembler::call_VM(noreg,
                               CAST_FROM_FN_PTR(address,
                               InterpreterRuntime::throw_pending_exception));
    __ should_not_reach_here();
    __ bind(L);
  }

  // do unlocking if necessary
  {
    Label L;
    __ lw(t, method, in_bytes(Method::access_flags_offset()));
    __ andi(t, t, JVM_ACC_SYNCHRONIZED);
    __ beq(t, R0, L);
    // the code below should be shared with interpreter macro assembler implementation
    {
      Label unlock;
      // BasicObjectLock will be first in list,
      // since this is a synchronized method. However, need
      // to check that the object has not been unlocked by
      // an explicit monitorexit bytecode.
      __ delayed()->daddiu(c_rarg0, FP, frame::interpreter_frame_initial_sp_offset * wordSize - (int)sizeof(BasicObjectLock));
      // address of first monitor

      __ ld(t, c_rarg0, BasicObjectLock::obj_offset_in_bytes());
      __ bne(t, R0, unlock);
      __ delayed()->nop();

      // Entry already unlocked, need to throw exception
      __ MacroAssembler::call_VM(NOREG, CAST_FROM_FN_PTR(address,
      InterpreterRuntime::throw_illegal_monitor_state_exception));
      __ should_not_reach_here();

      __ bind(unlock);
      __ unlock_object(c_rarg0);
    }
    __ bind(L);
  }

  // jvmti/jvmpi support
  // Note: This must happen _after_ handling/throwing any exceptions since
  //       the exception handler code notifies the runtime of method exits
  //       too. If this happens before, method entry/exit notifications are
  //       not properly paired (was bug - gri 11/22/99).
  __ notify_method_exit(vtos, InterpreterMacroAssembler::NotifyJVMTI);

  // restore potential result in V0,
  // call result handler to restore potential result in ST0 & handle result

  __ pop(ltos);
  __ pop(dtos);

  __ ld(t, FP, (frame::interpreter_frame_result_handler_offset) * wordSize);
  __ jalr(t);
  __ delayed()->nop();


  // remove activation
  __ ld(SP, FP, frame::interpreter_frame_sender_sp_offset * wordSize); // get sender sp
  __ ld(RA, FP, frame::interpreter_frame_return_addr_offset * wordSize); // get return address
  __ ld(FP, FP, frame::interpreter_frame_sender_fp_offset * wordSize); // restore sender's fp
  __ jr(RA);
  __ delayed()->nop();

#ifndef CORE
  if (inc_counter) {
    // Handle overflow of counter and compile method
    __ bind(invocation_counter_overflow);
    generate_counter_overflow(continue_after_compile);
    // entry_point is the beginning of this
    // function and checks again for compiled code
  }
#endif
  return entry_point;
}

void TemplateInterpreterGenerator::bang_stack_shadow_pages(bool native_call) {
  // Quick & dirty stack overflow checking: bang the stack & handle trap.
  // Note that we do the banging after the frame is setup, since the exception
  // handling code expects to find a valid interpreter frame on the stack.
  // Doing the banging earlier fails if the caller frame is not an interpreter
  // frame.
  // (Also, the exception throwing code expects to unlock any synchronized
  // method receiever, so do the banging after locking the receiver.)

  // Bang each page in the shadow zone. We can't assume it's been done for
  // an interpreter frame with greater than a page of locals, so each page
  // needs to be checked.  Only true for non-native.
  if (UseStackBanging) {
    const int page_size = os::vm_page_size();
    const int n_shadow_pages = ((int)JavaThread::stack_shadow_zone_size()) / page_size;
    const int start_page = native_call ? n_shadow_pages : 1;
    BLOCK_COMMENT("bang_stack_shadow_pages:");
    for (int pages = start_page; pages <= n_shadow_pages; pages++) {
      __ bang_stack_with_offset(pages*page_size);
    }
  }
}

//
// Generic interpreted method entry to (asm) interpreter
//
// Layout of frame just at the entry
//
//   [ argument word n-1  ] <--- sp
//     ...
//   [ argument word 0    ]
// assume Method* in Rmethod before call this method.
// prerequisites to the generated stub : the callee Method* in Rmethod
// note you must save the caller bcp before call the generated stub
//
address TemplateInterpreterGenerator::generate_normal_entry(bool synchronized) {
  // determine code generation flags
  bool inc_counter  = UseCompiler || CountCompiledCalls || LogTouchedMethods;

  // Rmethod: Method*
  // Rsender: sender 's sp
  address entry_point = __ pc();

  const Address invocation_counter(Rmethod,
      in_bytes(MethodCounters::invocation_counter_offset() + InvocationCounter::counter_offset()));

  // get parameter size (always needed)
  __ ld(T3, Rmethod, in_bytes(Method::const_offset()));  //T3 --> Rmethod._constMethod
  __ lhu(V0, T3, in_bytes(ConstMethod::size_of_parameters_offset()));

  // Rmethod: Method*
  // V0: size of parameters
  // Rsender: sender 's sp ,could be different frome sp+ wordSize if we call via c2i
  // get size of locals in words to T2
  __ lhu(T2, T3, in_bytes(ConstMethod::size_of_locals_offset()));
  // T2 = no. of additional locals, locals include parameters
  __ dsubu(T2, T2, V0);

  // see if we've got enough room on the stack for locals plus overhead.
  // Layout of frame at this point
  //
  // [ argument word n-1  ] <--- sp
  //   ...
  // [ argument word 0    ]
  generate_stack_overflow_check();
  // after this function, the layout of frame does not change

  // compute beginning of parameters (LVP)
  __ dsll(LVP, V0, LogBytesPerWord);
  __ daddiu(LVP, LVP, (-1) * wordSize);
  __ daddu(LVP, LVP, SP);

  // T2 - # of additional locals
  // allocate space for locals
  // explicitly initialize locals
  {
    Label exit, loop;
    __ beq(T2, R0, exit);
    __ delayed()->nop();

    __ bind(loop);
    __ daddiu(SP, SP, (-1) * wordSize);
    __ daddiu(T2, T2, -1);               // until everything initialized
    __ bne(T2, R0, loop);
    __ delayed()->sd(R0, SP, 0);     // initialize local variables

    __ bind(exit);
  }

  //
  // [ local var m-1      ] <--- sp
  //   ...
  // [ local var 0        ]
  // [ argument word n-1  ] <--- T0?
  //   ...
  // [ argument word 0    ] <--- LVP

  // initialize fixed part of activation frame

  generate_fixed_frame(false);


  // after this function, the layout of frame is as following
  //
  // [ monitor block top        ] <--- sp ( the top monitor entry )
  // [ byte code pointer        ] (if native, bcp = 0)
  // [ constant pool cache      ]
  // [ Method*                  ]
  // [ locals offset            ]
  // [ sender's sp              ]
  // [ sender's fp              ] <--- fp
  // [ return address           ]
  // [ local var m-1            ]
  //   ...
  // [ local var 0              ]
  // [ argumnet word n-1        ] <--- ( sender's sp )
  //   ...
  // [ argument word 0          ] <--- LVP


  // make sure method is not native & not abstract
#ifdef ASSERT
  __ ld(AT, Rmethod, in_bytes(Method::access_flags_offset()));
  {
    Label L;
    __ andi(T2, AT, JVM_ACC_NATIVE);
    __ beq(T2, R0, L);
    __ delayed()->nop();
    __ stop("tried to execute native method as non-native");
    __ bind(L);
  }
  {
    Label L;
    __ andi(T2, AT, JVM_ACC_ABSTRACT);
    __ beq(T2, R0, L);
    __ delayed()->nop();
    __ stop("tried to execute abstract method in interpreter");
    __ bind(L);
  }
#endif

  // Since at this point in the method invocation the exception handler
  // would try to exit the monitor of synchronized methods which hasn't
  // been entered yet, we set the thread local variable
  // _do_not_unlock_if_synchronized to true. The remove_activation will
  // check this flag.

#ifndef OPT_THREAD
  Register thread = T8;
  __ get_thread(thread);
#else
  Register thread = TREG;
#endif
  __ move(AT, (int)true);
  __ sb(AT, thread, in_bytes(JavaThread::do_not_unlock_if_synchronized_offset()));

#ifndef CORE

  // mdp : T8
  // tmp1: T9
  // tmp2: T2
   __ profile_parameters_type(T8, T9, T2);

  // increment invocation count & check for overflow
  Label invocation_counter_overflow;
  Label profile_method;
  Label profile_method_continue;
  if (inc_counter) {
    generate_counter_incr(&invocation_counter_overflow,
                          &profile_method,
                          &profile_method_continue);
    if (ProfileInterpreter) {
      __ bind(profile_method_continue);
    }
  }

  Label continue_after_compile;
  __ bind(continue_after_compile);

#endif // CORE

  bang_stack_shadow_pages(false);

  // reset the _do_not_unlock_if_synchronized flag
#ifndef OPT_THREAD
  __ get_thread(thread);
#endif
  __ sb(R0, thread, in_bytes(JavaThread::do_not_unlock_if_synchronized_offset()));

  // check for synchronized methods
  // Must happen AFTER invocation_counter check and stack overflow check,
  // so method is not locked if overflows.
  //
  if (synchronized) {
    // Allocate monitor and lock method
    lock_method();
  } else {
    // no synchronization necessary
#ifdef ASSERT
    { Label L;
      __ lw(AT, Rmethod, in_bytes(Method::access_flags_offset()));
      __ andi(T2, AT, JVM_ACC_SYNCHRONIZED);
      __ beq(T2, R0, L);
      __ delayed()->nop();
      __ stop("method needs synchronization");
      __ bind(L);
    }
#endif
  }

  // layout of frame after lock_method
  // [ monitor entry            ] <--- sp
  //   ...
  // [ monitor entry            ]
  // [ monitor block top        ] ( the top monitor entry )
  // [ byte code pointer        ] (if native, bcp = 0)
  // [ constant pool cache      ]
  // [ Method*                  ]
  // [ locals offset            ]
  // [ sender's sp              ]
  // [ sender's fp              ]
  // [ return address           ] <--- fp
  // [ local var m-1            ]
  //   ...
  // [ local var 0              ]
  // [ argumnet word n-1        ] <--- ( sender's sp )
  //   ...
  // [ argument word 0          ] <--- LVP


  // start execution
#ifdef ASSERT
  {
    Label L;
    __ ld(AT, FP, frame::interpreter_frame_monitor_block_top_offset * wordSize);
    __ beq(AT, SP, L);
    __ delayed()->nop();
    __ stop("broken stack frame setup in interpreter in native");
    __ bind(L);
  }
#endif

  // jvmti/jvmpi support
  __ notify_method_entry();

  __ dispatch_next(vtos);

  // invocation counter overflow
  if (inc_counter) {
    if (ProfileInterpreter) {
      // We have decided to profile this method in the interpreter
      __ bind(profile_method);
      __ call_VM(noreg, CAST_FROM_FN_PTR(address,
                 InterpreterRuntime::profile_method));
      __ set_method_data_pointer_for_bcp();
      __ get_method(Rmethod);
      __ b(profile_method_continue);
      __ delayed()->nop();
    }
    // Handle overflow of counter and compile method
    __ bind(invocation_counter_overflow);
    generate_counter_overflow(continue_after_compile);
  }

  return entry_point;
}

//-----------------------------------------------------------------------------
// Exceptions

void TemplateInterpreterGenerator::generate_throw_exception() {
  // Entry point in previous activation (i.e., if the caller was
  // interpreted)
  Interpreter::_rethrow_exception_entry = __ pc();
  // Restore sp to interpreter_frame_last_sp even though we are going
  // to empty the expression stack for the exception processing.
  __ sd(R0,FP, frame::interpreter_frame_last_sp_offset * wordSize);

  // V0: exception
  // V1: return address/pc that threw exception
  __ restore_bcp();                              // BCP points to call/send
  __ restore_locals();

  //add for compressedoops
  __ reinit_heapbase();
  // Entry point for exceptions thrown within interpreter code
  Interpreter::_throw_exception_entry = __ pc();
  // expression stack is undefined here
  // V0: exception
  // BCP: exception bcp
  __ verify_oop(V0);

  // expression stack must be empty before entering the VM in case of an exception
  __ empty_expression_stack();
  // find exception handler address and preserve exception oop
  __ move(A1, V0);
  __ call_VM(V1, CAST_FROM_FN_PTR(address, InterpreterRuntime::exception_handler_for_exception), A1);
  // V0: exception handler entry point
  // V1: preserved exception oop
  // S0: bcp for exception handler
  __ push(V1);                                 // push exception which is now the only value on the stack
  __ jr(V0);                                   // jump to exception handler (may be _remove_activation_entry!)
  __ delayed()->nop();

  // If the exception is not handled in the current frame the frame is removed and
  // the exception is rethrown (i.e. exception continuation is _rethrow_exception).
  //
  // Note: At this point the bci is still the bxi for the instruction which caused
  //       the exception and the expression stack is empty. Thus, for any VM calls
  //       at this point, GC will find a legal oop map (with empty expression stack).

  // In current activation
  // V0: exception
  // BCP: exception bcp

  //
  // JVMTI PopFrame support
  //

  Interpreter::_remove_activation_preserving_args_entry = __ pc();
  __ empty_expression_stack();
  // Set the popframe_processing bit in pending_popframe_condition indicating that we are
  // currently handling popframe, so that call_VMs that may happen later do not trigger new
  // popframe handling cycles.
#ifndef OPT_THREAD
  Register thread = T2;
  __ get_thread(T2);
#else
  Register thread = TREG;
#endif
  __ lw(T3, thread, in_bytes(JavaThread::popframe_condition_offset()));
  __ ori(T3, T3, JavaThread::popframe_processing_bit);
  __ sw(T3, thread, in_bytes(JavaThread::popframe_condition_offset()));

#ifndef CORE
  {
    // Check to see whether we are returning to a deoptimized frame.
    // (The PopFrame call ensures that the caller of the popped frame is
    // either interpreted or compiled and deoptimizes it if compiled.)
    // In this case, we can't call dispatch_next() after the frame is
    // popped, but instead must save the incoming arguments and restore
    // them after deoptimization has occurred.
    //
    // Note that we don't compare the return PC against the
    // deoptimization blob's unpack entry because of the presence of
    // adapter frames in C2.
    Label caller_not_deoptimized;
    __ ld(A0, FP, frame::return_addr_offset * wordSize);
    __ super_call_VM_leaf(CAST_FROM_FN_PTR(address, InterpreterRuntime::interpreter_contains), A0);
    __ bne(V0, R0, caller_not_deoptimized);
    __ delayed()->nop();

    // Compute size of arguments for saving when returning to deoptimized caller
    __ get_method(A1);
    __ verify_oop(A1);
    __ ld( A1, A1, in_bytes(Method::const_offset()));
    __ lhu(A1, A1, in_bytes(ConstMethod::size_of_parameters_offset()));
    __ shl(A1, Interpreter::logStackElementSize);
    __ restore_locals();
    __ dsubu(A2, LVP, A1);
    __ daddiu(A2, A2, wordSize);
    // Save these arguments
#ifndef OPT_THREAD
    __ get_thread(A0);
#else
    __ move(A0, TREG);
#endif
    __ super_call_VM_leaf(CAST_FROM_FN_PTR(address, Deoptimization::popframe_preserve_args), A0, A1, A2);

    __ remove_activation(vtos, T9, false, false, false);

    // Inform deoptimization that it is responsible for restoring these arguments
#ifndef OPT_THREAD
    __ get_thread(thread);
#endif
    __ move(AT, JavaThread::popframe_force_deopt_reexecution_bit);
    __ sw(AT, thread, in_bytes(JavaThread::popframe_condition_offset()));
    // Continue in deoptimization handler
    __ jr(T9);
    __ delayed()->nop();

    __ bind(caller_not_deoptimized);
  }
#endif /* !CORE */

  __ remove_activation(vtos, T3,
                       /* throw_monitor_exception */ false,
                       /* install_monitor_exception */ false,
                       /* notify_jvmdi */ false);

  // Clear the popframe condition flag
  // Finish with popframe handling
  // A previous I2C followed by a deoptimization might have moved the
  // outgoing arguments further up the stack. PopFrame expects the
  // mutations to those outgoing arguments to be preserved and other
  // constraints basically require this frame to look exactly as
  // though it had previously invoked an interpreted activation with
  // no space between the top of the expression stack (current
  // last_sp) and the top of stack. Rather than force deopt to
  // maintain this kind of invariant all the time we call a small
  // fixup routine to move the mutated arguments onto the top of our
  // expression stack if necessary.
  __ move(T8, SP);
  __ ld(A2, FP, frame::interpreter_frame_last_sp_offset * wordSize);
#ifndef OPT_THREAD
  __ get_thread(thread);
#endif
  // PC must point into interpreter here
  __ set_last_Java_frame(thread, noreg, FP, __ pc());
  __ super_call_VM_leaf(CAST_FROM_FN_PTR(address, InterpreterRuntime::popframe_move_outgoing_args), thread, T8, A2);
  __ reset_last_Java_frame(thread, true);
  // Restore the last_sp and null it out
  __ ld(SP, FP, frame::interpreter_frame_last_sp_offset * wordSize);
  __ sd(R0, FP, frame::interpreter_frame_last_sp_offset * wordSize);



  __ move(AT, JavaThread::popframe_inactive);
  __ sw(AT, thread, in_bytes(JavaThread::popframe_condition_offset()));

  // Finish with popframe handling
  __ restore_bcp();
  __ restore_locals();
#ifndef CORE
  // The method data pointer was incremented already during
  // call profiling. We have to restore the mdp for the current bcp.
  if (ProfileInterpreter) {
    __ set_method_data_pointer_for_bcp();
  }
#endif // !CORE
  // Clear the popframe condition flag
#ifndef OPT_THREAD
  __ get_thread(thread);
#endif
  __ move(AT, JavaThread::popframe_inactive);
  __ sw(AT, thread, in_bytes(JavaThread::popframe_condition_offset()));

#if INCLUDE_JVMTI
  {
    Label L_done;

    __ lbu(AT, BCP, 0);
    __ daddiu(AT, AT, -1 * Bytecodes::_invokestatic);
    __ bne(AT, R0, L_done);
    __ delayed()->nop();

    // The member name argument must be restored if _invokestatic is re-executed after a PopFrame call.
    // Detect such a case in the InterpreterRuntime function and return the member name argument, or NULL.

    __ get_method(T9);
    __ ld(T8, LVP, 0);
    __ call_VM(T8, CAST_FROM_FN_PTR(address, InterpreterRuntime::member_name_arg_or_null), T8, T9, BCP);

    __ beq(T8, R0, L_done);
    __ delayed()->nop();

    __ sd(T8, SP, 0);
    __ bind(L_done);
  }
#endif // INCLUDE_JVMTI

  __ dispatch_next(vtos);
  // end of PopFrame support

  Interpreter::_remove_activation_entry = __ pc();

  // preserve exception over this code sequence
  __ pop(T0);
#ifndef OPT_THREAD
  __ get_thread(thread);
#endif
  __ sd(T0, thread, in_bytes(JavaThread::vm_result_offset()));
  // remove the activation (without doing throws on illegalMonitorExceptions)
  __ remove_activation(vtos, T3, false, true, false);
  // restore exception
  __ get_vm_result(T0, thread);
  __ verify_oop(T0);

  // In between activations - previous activation type unknown yet
  // compute continuation point - the continuation point expects
  // the following registers set up:
  //
  // T0: exception
  // T1: return address/pc that threw exception
  // SP: expression stack of caller
  // FP: fp of caller
  __ push2(T0, T3);             // save exception and return address
  __ move(A1, T3);
  __ super_call_VM_leaf(CAST_FROM_FN_PTR(address, SharedRuntime::exception_handler_for_return_address), thread, A1);
  __ move(T9, V0);                             // save exception handler
  __ pop2(V0, V1);                   // restore return address and exception

  // Note that an "issuing PC" is actually the next PC after the call
  __ jr(T9);                                   // jump to exception handler of caller
  __ delayed()->nop();
}


//
// JVMTI ForceEarlyReturn support
//
address TemplateInterpreterGenerator::generate_earlyret_entry_for(TosState state) {
  address entry = __ pc();
  __ restore_bcp();
  __ restore_locals();
  __ empty_expression_stack();
  __ empty_FPU_stack();
  __ load_earlyret_value(state);

#ifndef OPT_THREAD
  __ get_thread(TREG);
#endif
  __ ld_ptr(T9, TREG, in_bytes(JavaThread::jvmti_thread_state_offset()));
  const Address cond_addr(T9, in_bytes(JvmtiThreadState::earlyret_state_offset()));
  // Clear the earlyret state
  __ move(AT, JvmtiThreadState::earlyret_inactive);
  __ sw(AT, cond_addr);
  __ sync();


  __ remove_activation(state, T0,
                         false, /* throw_monitor_exception */
                         false, /* install_monitor_exception */
                         true); /* notify_jvmdi */
  __ sync();
  __ jr(T0);
  __ delayed()->nop();
  return entry;
} // end of ForceEarlyReturn support


//-----------------------------------------------------------------------------
// Helper for vtos entry point generation

void TemplateInterpreterGenerator::set_vtos_entry_points(Template* t,
                                                         address& bep,
                                                         address& cep,
                                                         address& sep,
                                                         address& aep,
                                                         address& iep,
                                                         address& lep,
                                                         address& fep,
                                                         address& dep,
                                                         address& vep) {
  assert(t->is_valid() && t->tos_in() == vtos, "illegal template");
  Label L;
  fep = __ pc(); __ push(ftos); __ b(L); __ delayed()->nop();
  dep = __ pc(); __ push(dtos); __ b(L); __ delayed()->nop();
  lep = __ pc(); __ push(ltos); __ b(L); __ delayed()->nop();
  aep  =__ pc(); __ push(atos); __ b(L); __ delayed()->nop();
  bep = cep = sep =
  iep = __ pc(); __ push(itos);
  vep = __ pc();
  __ bind(L);
  generate_and_dispatch(t);
}


/*
//-----------------------------------------------------------------------------
// Generation of individual instructions

// helpers for generate_and_dispatch


InterpreterGenerator::InterpreterGenerator(StubQueue* code)
  : TemplateInterpreterGenerator(code) {
   generate_all(); // down here so it can be "virtual"
}
*/

//-----------------------------------------------------------------------------

// Non-product code
#ifndef PRODUCT
address TemplateInterpreterGenerator::generate_trace_code(TosState state) {
  address entry = __ pc();

  // prepare expression stack
  __ push(state);       // save tosca

  // tos & tos2
  // trace_bytecode need actually 4 args, the last two is tos&tos2
  // this work fine for x86. but mips o32 call convention will store A2-A3
  // to the stack position it think is the tos&tos2
  // when the expression stack have no more than 2 data, error occur.
  __ ld(A2, SP, 0);
  __ ld(A3, SP, 1 * wordSize);

  // pass arguments & call tracer
  __ call_VM(noreg, CAST_FROM_FN_PTR(address, InterpreterRuntime::trace_bytecode), RA, A2, A3);
  __ move(RA, V0);    // make sure return address is not destroyed by pop(state)

  // restore expression stack
  __ pop(state);        // restore tosca

  // return
  __ jr(RA);
  __ delayed()->nop();

  return entry;
}

void TemplateInterpreterGenerator::count_bytecode() {
  __ li(T8, (long)&BytecodeCounter::_counter_value);
  __ lw(AT, T8, 0);
  __ daddiu(AT, AT, 1);
  __ sw(AT, T8, 0);
}

void TemplateInterpreterGenerator::histogram_bytecode(Template* t) {
  __ li(T8, (long)&BytecodeHistogram::_counters[t->bytecode()]);
  __ lw(AT, T8, 0);
  __ daddiu(AT, AT, 1);
  __ sw(AT, T8, 0);
}

void TemplateInterpreterGenerator::histogram_bytecode_pair(Template* t) {
  __ li(T8, (long)&BytecodePairHistogram::_index);
  __ lw(T9, T8, 0);
  __ dsrl(T9, T9, BytecodePairHistogram::log2_number_of_codes);
  __ li(T8, ((long)t->bytecode()) << BytecodePairHistogram::log2_number_of_codes);
  __ orr(T9, T9, T8);
  __ li(T8, (long)&BytecodePairHistogram::_index);
  __ sw(T9, T8, 0);
  __ dsll(T9, T9, 2);
  __ li(T8, (long)BytecodePairHistogram::_counters);
  __ daddu(T8, T8, T9);
  __ lw(AT, T8, 0);
  __ daddiu(AT, AT, 1);
  __ sw(AT, T8, 0);
}


void TemplateInterpreterGenerator::trace_bytecode(Template* t) {
  // Call a little run-time stub to avoid blow-up for each bytecode.
  // The run-time runtime saves the right registers, depending on
  // the tosca in-state for the given template.

  address entry = Interpreter::trace_code(t->tos_in());
  assert(entry != NULL, "entry must have been generated");
  __ call(entry, relocInfo::none);
  __ delayed()->nop();
  //add for compressedoops
  __ reinit_heapbase();
}


void TemplateInterpreterGenerator::stop_interpreter_at() {
  Label L;
  __ li(T8, long(&BytecodeCounter::_counter_value));
  __ lw(T8, T8, 0);
  __ move(AT, StopInterpreterAt);
  __ bne(T8, AT, L);
  __ delayed()->nop();
  __ brk(5);
  __ delayed()->nop();
  __ bind(L);
}
#endif // !PRODUCT
