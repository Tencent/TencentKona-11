/*
 * Copyright (c) 2003, 2013, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2015, 2023, Loongson Technology. All rights reserved.
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

#ifndef OS_CPU_LINUX_LOONGARCH_ORDERACCESS_LINUX_LOONGARCH_HPP
#define OS_CPU_LINUX_LOONGARCH_ORDERACCESS_LINUX_LOONGARCH_HPP

#include "runtime/globals.hpp"
#include "runtime/os.hpp"

// Included in orderAccess.hpp header file.

// Implementation of class OrderAccess.
#define inlasm_sync(v) if (!UseActiveCoresMP) \
                        __asm__ __volatile__ ("dbar %0"   : :"K"(v) : "memory");

inline void OrderAccess::loadload()   { inlasm_sync(0x15); }
inline void OrderAccess::storestore() { inlasm_sync(0x1a); }
inline void OrderAccess::loadstore()  { inlasm_sync(0x16); }
inline void OrderAccess::storeload()  { inlasm_sync(0x19); }

inline void OrderAccess::acquire() { inlasm_sync(0x14); }
inline void OrderAccess::release() { inlasm_sync(0x12); }
inline void OrderAccess::fence()   { inlasm_sync(0x10); }


#undef inlasm_sync

#endif // OS_CPU_LINUX_LOONGARCH_ORDERACCESS_LINUX_LOONGARCH_HPP
