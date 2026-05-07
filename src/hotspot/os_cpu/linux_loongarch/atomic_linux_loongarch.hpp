/*
 * Copyright (c) 1999, 2013, Oracle and/or its affiliates. All rights reserved.
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

#ifndef OS_CPU_LINUX_LOONGARCH_ATOMIC_LINUX_LOONGARCH_HPP
#define OS_CPU_LINUX_LOONGARCH_ATOMIC_LINUX_LOONGARCH_HPP

#include "runtime/vm_version.hpp"

// Implementation of class atomic

template<size_t byte_size>
struct Atomic::PlatformAdd
  : Atomic::AddAndFetch<Atomic::PlatformAdd<byte_size> >
{
  template<typename I, typename D>
  D add_and_fetch(I add_value, D volatile* dest, atomic_memory_order order) const {
    //Unimplemented();
    return __sync_add_and_fetch(dest, add_value);
  }
};

template<>
template<typename T>
inline T Atomic::PlatformXchg<4>::operator()(T exchange_value,
                                             T volatile* dest,
                                             atomic_memory_order order) const {
  T __ret, __tmp;

  STATIC_ASSERT(4 == sizeof(T));
  __asm__ __volatile__ (
      "1: ll.w  %[__ret], %[__dest]  \n\t"
      "   move  %[__tmp], %[__val]  \n\t"
      "   sc.w  %[__tmp], %[__dest]  \n\t"
      "   beqz  %[__tmp], 1b    \n\t"

      : [__ret] "=&r" (__ret), [__tmp] "=&r" (__tmp)
      : [__dest] "ZC" (*(volatile jint*)dest), [__val] "r" (exchange_value)
      : "memory"
      );

  return __ret;
}

template<>
template<typename T>
inline T Atomic::PlatformXchg<8>::operator()(T exchange_value,
                                             T volatile* dest,
                                             atomic_memory_order order) const {
  STATIC_ASSERT(8 == sizeof(T));
  T __ret;
  jlong __tmp;
  __asm__ __volatile__ (
      "1: ll.d  %[__ret], %[__dest]  \n\t"
      "   move  %[__tmp], %[__val]  \n\t"
      "   sc.d  %[__tmp], %[__dest]  \n\t"
      "   beqz  %[__tmp], 1b    \n\t"

      : [__ret] "=&r" (__ret), [__tmp] "=&r" (__tmp)
      : [__dest] "ZC" (*(volatile intptr_t*)dest), [__val] "r" (exchange_value)
      : "memory"
      );

  return __ret;
}

#if 0
template<>
template<typename T>
inline T Atomic::PlatformCmpxchg<1>::operator()(T exchange_value,
                                                        T volatile* dest,
                                                        T compare_value,
                                                        atomic_memory_order order) const {
  STATIC_ASSERT(1 == sizeof(T));
}

#else
// No direct support for cmpxchg of bytes; emulate using int.
template<>
struct Atomic::PlatformCmpxchg<1> : Atomic::CmpxchgByteUsingInt {};
#endif

template<>
template<typename T>
inline T Atomic::PlatformCmpxchg<4>::operator()(T exchange_value,
                                                        T volatile* dest,
                                                        T compare_value,
                                                        atomic_memory_order order) const {
  STATIC_ASSERT(4 == sizeof(T));
  T __prev;
  jint __cmp;

  __asm__ __volatile__ (
      "1: ll.w  %[__prev], %[__dest]    \n\t"
      "   bne   %[__prev], %[__old], 2f  \n\t"
      "   move  %[__cmp],  $r0          \n\t"
      "   move  %[__cmp],  %[__new]  \n\t"
      "   sc.w  %[__cmp],  %[__dest]  \n\t"
      "   beqz  %[__cmp],  1b    \n\t"
      "2:        \n\t"
      "  dbar 0x700        \n\t"

      : [__prev] "=&r" (__prev), [__cmp] "=&r" (__cmp)
      : [__dest] "ZC" (*(volatile jint*)dest), [__old] "r" (compare_value),  [__new] "r" (exchange_value)
      : "memory"
      );

  return __prev;
}

template<>
template<typename T>
inline T Atomic::PlatformCmpxchg<8>::operator()(T exchange_value,
                                                        T volatile* dest,
                                                        T compare_value,
                                                        atomic_memory_order order) const {
  STATIC_ASSERT(8 == sizeof(T));
  T __prev;
  jlong __cmp;

  __asm__ __volatile__ (
      "1: ll.d  %[__prev], %[__dest]    \n\t"
      "   bne   %[__prev], %[__old], 2f  \n\t"
      "   move  %[__cmp],  $r0          \n\t"
      "   move  %[__cmp],  %[__new]  \n\t"
      "   sc.d  %[__cmp],  %[__dest]  \n\t"
      "   beqz  %[__cmp],  1b    \n\t"
      "2:        \n\t"
      "   dbar 0x700 \n\t"

      : [__prev] "=&r" (__prev), [__cmp] "=&r" (__cmp)
      : [__dest] "ZC" (*(volatile jlong*)dest), [__old] "r" (compare_value),  [__new] "r" (exchange_value)
      : "memory"
      );
  return __prev;
}


#endif // OS_CPU_LINUX_LOONGARCH_ATOMIC_LINUX_LOONGARCH_HPP
