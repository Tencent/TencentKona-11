/*
 * Copyright (c) 2006, 2021, Oracle and/or its affiliates. All rights reserved.
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

#include "precompiled.hpp"
#include "runtime/os.hpp"
#include "runtime/vm_version.hpp"

#include <asm/hwcap.h>
#include <sys/auxv.h>

#ifndef HWCAP_LOONGARCH_LAM
#define HWCAP_LOONGARCH_LAM       (1 << 1)
#endif

#ifndef HWCAP_LOONGARCH_UAL
#define HWCAP_LOONGARCH_UAL       (1 << 2)
#endif

#ifndef HWCAP_LOONGARCH_LSX
#define HWCAP_LOONGARCH_LSX       (1 << 4)
#endif

#ifndef HWCAP_LOONGARCH_LASX
#define HWCAP_LOONGARCH_LASX      (1 << 5)
#endif

#ifndef HWCAP_LOONGARCH_COMPLEX
#define HWCAP_LOONGARCH_COMPLEX   (1 << 7)
#endif

#ifndef HWCAP_LOONGARCH_CRYPTO
#define HWCAP_LOONGARCH_CRYPTO    (1 << 8)
#endif

#ifndef HWCAP_LOONGARCH_LBT_X86
#define HWCAP_LOONGARCH_LBT_X86   (1 << 10)
#endif

#ifndef HWCAP_LOONGARCH_LBT_ARM
#define HWCAP_LOONGARCH_LBT_ARM   (1 << 11)
#endif

#ifndef HWCAP_LOONGARCH_LBT_MIPS
#define HWCAP_LOONGARCH_LBT_MIPS  (1 << 12)
#endif

void VM_Version::get_os_cpu_info() {

  uint64_t auxv = getauxval(AT_HWCAP);

  STATIC_ASSERT(CPU_LAM      == HWCAP_LOONGARCH_LAM);
  STATIC_ASSERT(CPU_UAL      == HWCAP_LOONGARCH_UAL);
  STATIC_ASSERT(CPU_LSX      == HWCAP_LOONGARCH_LSX);
  STATIC_ASSERT(CPU_LASX     == HWCAP_LOONGARCH_LASX);
  STATIC_ASSERT(CPU_COMPLEX  == HWCAP_LOONGARCH_COMPLEX);
  STATIC_ASSERT(CPU_CRYPTO   == HWCAP_LOONGARCH_CRYPTO);
  STATIC_ASSERT(CPU_LBT_X86  == HWCAP_LOONGARCH_LBT_X86);
  STATIC_ASSERT(CPU_LBT_ARM  == HWCAP_LOONGARCH_LBT_ARM);
  STATIC_ASSERT(CPU_LBT_MIPS == HWCAP_LOONGARCH_LBT_MIPS);

  _features = auxv & (
      HWCAP_LOONGARCH_LAM     |
      HWCAP_LOONGARCH_UAL     |
      HWCAP_LOONGARCH_LSX     |
      HWCAP_LOONGARCH_LASX    |
      HWCAP_LOONGARCH_COMPLEX |
      HWCAP_LOONGARCH_CRYPTO  |
      HWCAP_LOONGARCH_LBT_X86 |
      HWCAP_LOONGARCH_LBT_ARM |
      HWCAP_LOONGARCH_LBT_MIPS);
}
