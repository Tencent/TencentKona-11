/*
 * Copyright (c) 2018, Oracle and/or its affiliates. All rights reserved.
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
#include "classfile/systemDictionary.hpp"
#include "gc/shared/weakProcessorPhases.hpp"
#include "prims/resolvedMethodTable.hpp"
#include "runtime/jniHandles.hpp"
#include "utilities/debug.hpp"
#include "utilities/macros.hpp"

#if INCLUDE_JFR
#include "jfr/jfr.hpp"
#endif // INCLUDE_JFR

#if INCLUDE_JVMTI
#include "prims/jvmtiExport.hpp"
#endif // INCLUDE_JVMTI

WeakProcessorPhases::Phase WeakProcessorPhases::phase(uint value) {
  assert(value < phase_count, "Invalid phase value %u", value);
  return static_cast<Phase>(value);
}

uint WeakProcessorPhases::index(Phase phase) {
  uint value = static_cast<uint>(phase);
  assert(value < phase_count, "Invalid phase %u", value);
  return value;
}

uint WeakProcessorPhases::serial_index(Phase phase) {
  assert(is_serial(phase), "not serial phase %u", index(phase));
  return index(phase) - serial_phase_start;
}

uint WeakProcessorPhases::oop_storage_index(Phase phase) {
  assert(is_oop_storage(phase), "not oop storage phase %u", index(phase));
  return index(phase) - oop_storage_phase_start;
}

bool WeakProcessorPhases::is_serial(Phase phase) {
  return (index(phase) - serial_phase_start) < serial_phase_count;
}

bool WeakProcessorPhases::is_oop_storage(Phase phase) {
  return (index(phase) - oop_storage_phase_start) < oop_storage_phase_count;
}

const char* WeakProcessorPhases::description(Phase phase) {
  switch (phase) {
  JVMTI_ONLY(case jvmti: return "JVMTI weak processing";)
  JFR_ONLY(case jfr: return "JFR weak processing";)
  case jni: return "JNI weak processing";
  case resolved_method_table: return "ResolvedMethodTable weak processing";
  case vm: return "VM weak processing";
  default:
    ShouldNotReachHere();
    return "Invalid weak processing phase";
  }
}

WeakProcessorPhases::Processor WeakProcessorPhases::processor(Phase phase) {
  switch (phase) {
  JVMTI_ONLY(case jvmti: return &JvmtiExport::weak_oops_do;)
  JFR_ONLY(case jfr: return &Jfr::weak_oops_do;)
  default:
    ShouldNotReachHere();
    return NULL;
  }
}

OopStorage* WeakProcessorPhases::oop_storage(Phase phase) {
  switch (phase) {
  case jni: return JNIHandles::weak_global_handles();
  case resolved_method_table: return ResolvedMethodTable::weak_storage();
  case vm: return SystemDictionary::vm_weak_oop_storage();
  default:
    ShouldNotReachHere();
    return NULL;
  }
}

bool WeakProcessorPhases::is_resolved_method_table(Phase phase) {
  return phase == resolved_method_table;
}
