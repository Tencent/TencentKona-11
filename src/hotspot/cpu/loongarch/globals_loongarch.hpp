/*
 * Copyright (c) 2000, 2013, Oracle and/or its affiliates. All rights reserved.
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

#ifndef CPU_LOONGARCH_GLOBALS_LOONGARCH_HPP
#define CPU_LOONGARCH_GLOBALS_LOONGARCH_HPP

#include "utilities/globalDefinitions.hpp"
#include "utilities/macros.hpp"

// Sets the default values for platform dependent flags used by the runtime system.
// (see globals.hpp)

define_pd_global(bool, ShareVtableStubs,         true);
define_pd_global(bool, NeedsDeoptSuspend,        false); // only register window machines need this

define_pd_global(bool, ImplicitNullChecks,       true);  // Generate code for implicit null checks
define_pd_global(bool, TrapBasedNullChecks,      false); // Not needed on x86.
define_pd_global(bool, UncommonNullCast,         true);  // Uncommon-trap NULLs passed to check cast

define_pd_global(uintx, CodeCacheSegmentSize,    64 TIERED_ONLY(+64)); // Tiered compilation has large code-entry alignment.
define_pd_global(intx, CodeEntryAlignment,       16);
define_pd_global(intx, OptoLoopAlignment,        16);
define_pd_global(intx, InlineFrequencyCount,     100);
define_pd_global(intx, InlineSmallCode,          2000);

#define DEFAULT_STACK_YELLOW_PAGES (2)
#define DEFAULT_STACK_RED_PAGES (1)
#define DEFAULT_STACK_SHADOW_PAGES (20 DEBUG_ONLY(+4))
#define DEFAULT_STACK_RESERVED_PAGES (1)

#define MIN_STACK_YELLOW_PAGES DEFAULT_STACK_YELLOW_PAGES
#define MIN_STACK_RED_PAGES    DEFAULT_STACK_RED_PAGES
#define MIN_STACK_SHADOW_PAGES DEFAULT_STACK_SHADOW_PAGES
#define MIN_STACK_RESERVED_PAGES (0)
define_pd_global(intx, StackReservedPages, DEFAULT_STACK_RESERVED_PAGES);

define_pd_global(intx, StackYellowPages, 2);
define_pd_global(intx, StackRedPages, 1);
define_pd_global(intx, StackShadowPages, DEFAULT_STACK_SHADOW_PAGES);

define_pd_global(bool, RewriteBytecodes,     true);
define_pd_global(bool, RewriteFrequentPairs, true);
define_pd_global(bool, UseMembar,            true);
// GC Ergo Flags
define_pd_global(intx, CMSYoungGenPerWorker, 64*M);  // default max size of CMS young gen, per GC worker thread

define_pd_global(uintx, TypeProfileLevel, 111);

define_pd_global(bool, CompactStrings, true);

define_pd_global(bool, PreserveFramePointer, false);

define_pd_global(intx, InitArrayShortSize, 8*BytesPerLong);

define_pd_global(bool, ThreadLocalHandshakes, true);
// Only c2 cares about this at the moment
define_pd_global(intx, AllocatePrefetchStyle,        2);
define_pd_global(intx, AllocatePrefetchDistance,     -1);

#define ARCH_FLAGS(develop, \
                   product, \
                   diagnostic, \
                   experimental, \
                   notproduct, \
                   range, \
                   constraint, \
                   writeable) \
                                                                            \
  product(bool, UseCodeCacheAllocOpt, true,                                 \
                "Allocate code cache within 32-bit memory address space")   \
                                                                            \
  product(bool, UseLSX, false,                                              \
                "Use LSX 128-bit vector instructions")                      \
                                                                            \
  product(bool, UseLASX, false,                                             \
                "Use LASX 256-bit vector instructions")                     \
                                                                            \
  product(bool, UseBarriersForVolatile, false,                              \
          "Use memory barriers to implement volatile accesses")             \
                                                                            \
  product(bool, UseCRC32, false,                                            \
          "Use CRC32 instructions for CRC32 computation")                   \
                                                                            \
  product(bool, UseActiveCoresMP, false,                                    \
                "Eliminate barriers for single active cpu")

#endif // CPU_LOONGARCH_GLOBALS_LOONGARCH_HPP
