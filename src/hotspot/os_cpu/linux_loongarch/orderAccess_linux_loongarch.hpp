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

#ifndef OS_CPU_LINUX_LOONGARCH_ORDERACCESS_LINUX_LOONGARCH_HPP
#define OS_CPU_LINUX_LOONGARCH_ORDERACCESS_LINUX_LOONGARCH_HPP

#include "runtime/os.hpp"

// Included in orderAccess.hpp header file.

// Implementation of class OrderAccess.
#define inlasm_sync() if (os::is_ActiveCoresMP()) \
                        __asm__ __volatile__ ("nop"   : : : "memory"); \
                      else \
                        __asm__ __volatile__ ("dbar 0"   : : : "memory");

inline void OrderAccess::loadload()   { inlasm_sync(); }
inline void OrderAccess::storestore() { inlasm_sync(); }
inline void OrderAccess::loadstore()  { inlasm_sync(); }
inline void OrderAccess::storeload()  { inlasm_sync(); }

inline void OrderAccess::acquire() { inlasm_sync(); }
inline void OrderAccess::release() { inlasm_sync(); }
inline void OrderAccess::fence()   { inlasm_sync(); }


#undef inlasm_sync

#endif // OS_CPU_LINUX_LOONGARCH_ORDERACCESS_LINUX_LOONGARCH_HPP
