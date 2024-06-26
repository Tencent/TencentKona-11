/*
 * Copyright (c) 2011, 2018, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_VM_GC_G1_G1MONITORINGSUPPORT_HPP
#define SHARE_VM_GC_G1_G1MONITORINGSUPPORT_HPP

#include "gc/shared/generationCounters.hpp"

class CollectorCounters;
class G1CollectedHeap;
class HSpaceCounters;

// Class for monitoring logical spaces in G1. It provides data for
// both G1's jstat counters as well as G1's memory pools.
//
// G1 splits the heap into heap regions and each heap region belongs
// to one of the following categories:
//
// * eden      : regions that have been allocated since the last GC
// * survivors : regions with objects that survived the last few GCs
// * old       : long-lived non-humongous regions
// * humongous : humongous regions
// * free      : free regions
//
// The combination of eden and survivor regions form the equivalent of
// the young generation in the other GCs. The combination of old and
// humongous regions form the equivalent of the old generation in the
// other GCs. Free regions do not have a good equivalent in the other
// GCs given that they can be allocated as any of the other region types.
//
// The monitoring tools expect the heap to contain a number of
// generations (young, old, perm) and each generation to contain a
// number of spaces (young: eden, survivors, old). Given that G1 does
// not maintain those spaces physically (e.g., the set of
// non-contiguous eden regions can be considered as a "logical"
// space), we'll provide the illusion that those generations and
// spaces exist. In reality, each generation and space refers to a set
// of heap regions that are potentially non-contiguous.
//
// This class provides interfaces to access the min, current, and max
// capacity and current occupancy for each of G1's logical spaces and
// generations we expose to the monitoring tools. Also provided are
// counters for G1 concurrent collections and stop-the-world full heap
// collections.
//
// Below is a description of how the various sizes are calculated.
//
// * Current Capacity
//
//    - heap_capacity = current heap capacity (e.g., current committed size)
//    - young_gen_capacity = current max young gen target capacity
//          (i.e., young gen target capacity + max allowed expansion capacity)
//    - survivor_capacity = current survivor region capacity
//    - eden_capacity = young_gen_capacity - survivor_capacity
//    - old_capacity = heap_capacity - young_gen_capacity
//
//    What we do in the above is to distribute the free regions among
//    eden_capacity and old_capacity.
//
// * Occupancy
//
//    - young_gen_used = current young region capacity
//    - survivor_used = survivor_capacity
//    - eden_used = young_gen_used - survivor_used
//    - old_used = overall_used - young_gen_used
//
//    Unfortunately, we currently only keep track of the number of
//    currently allocated young and survivor regions + the overall used
//    bytes in the heap, so the above can be a little inaccurate.
//
// * Min Capacity
//
//    We set this to 0 for all spaces.
//
// * Max Capacity
//
//    For jstat, we set the max capacity of all spaces to heap_capacity,
//    given that we don't always have a reasonable upper bound on how big
//    each space can grow. For the memory pools, we make the max
//    capacity undefined with the exception of the old memory pool for
//    which we make the max capacity same as the max heap capacity.
//
// If we had more accurate occupancy / capacity information per
// region set the above calculations would be greatly simplified and
// be made more accurate.
//
// We update all the above synchronously and we store the results in
// fields so that we just read said fields when needed. A subtle point
// is that all the above sizes need to be recalculated when the old
// gen changes capacity (after a GC or after a humongous allocation)
// but only the eden occupancy changes when a new eden region is
// allocated. So, in the latter case we have minimal recalculation to
// do which is important as we want to keep the eden region allocation
// path as low-overhead as possible.

class G1MonitoringSupport : public CHeapObj<mtGC> {
  friend class VMStructs;

  G1CollectedHeap* _g1h;

  // jstat performance counters
  //  incremental collections both young and mixed
  CollectorCounters*   _incremental_collection_counters;
  //  full stop-the-world collections
  CollectorCounters*   _full_collection_counters;
  //  stop-the-world phases in G1
  CollectorCounters*   _conc_collection_counters;
  //  young collection set counters.  The _eden_counters,
  // _from_counters, and _to_counters are associated with
  // this "generational" counter.
  GenerationCounters*  _young_collection_counters;
  //  old collection set counters. The _old_space_counters
  // below are associated with this "generational" counter.
  GenerationCounters*  _old_collection_counters;
  // Counters for the capacity and used for
  //   the whole heap
  HSpaceCounters*      _old_space_counters;
  //   the young collection
  HSpaceCounters*      _eden_counters;
  //   the survivor collection (only one, _to_counters, is actively used)
  HSpaceCounters*      _from_counters;
  HSpaceCounters*      _to_counters;

  // When it's appropriate to recalculate the various sizes (at the
  // end of a GC, when a new eden region is allocated, etc.) we store
  // them here so that we can easily report them when needed and not
  // have to recalculate them every time.

  size_t _overall_reserved;
  size_t _overall_committed;
  size_t _overall_used;

  uint   _young_region_num;
  size_t _young_gen_committed;
  size_t _eden_committed;
  size_t _eden_used;
  size_t _survivor_committed;
  size_t _survivor_used;

  size_t _old_committed;
  size_t _old_used;

  // It returns x - y if x > y, 0 otherwise.
  // As described in the comment above, some of the inputs to the
  // calculations we have to do are obtained concurrently and hence
  // may be inconsistent with each other. So, this provides a
  // defensive way of performing the subtraction and avoids the value
  // going negative (which would mean a very large result, given that
  // the parameter are size_t).
  static size_t subtract_up_to_zero(size_t x, size_t y) {
    if (x > y) {
      return x - y;
    } else {
      return 0;
    }
  }

  // Recalculate all the sizes.
  void recalculate_sizes();
  // Recalculate only what's necessary when a new eden region is allocated.
  void recalculate_eden_size();

 public:
  G1MonitoringSupport(G1CollectedHeap* g1h);

  // Unfortunately, the jstat tool assumes that no space has 0
  // capacity. In our case, given that each space is logical, it's
  // possible that no regions will be allocated to it, hence to have 0
  // capacity (e.g., if there are no survivor regions, the survivor
  // space has 0 capacity). The way we deal with this is to always pad
  // each capacity value we report to jstat by a very small amount to
  // make sure that it's never zero. Given that we sometimes have to
  // report a capacity of a generation that contains several spaces
  // (e.g., young gen includes one eden, two survivor spaces), the
  // mult parameter is provided in order to adding the appropriate
  // padding multiple times so that the capacities add up correctly.
  static size_t pad_capacity(size_t size_bytes, size_t mult = 1) {
    return size_bytes + MinObjAlignmentInBytes * mult;
  }

  // Recalculate all the sizes from scratch and update all the jstat
  // counters accordingly.
  void update_sizes();
  // Recalculate only what's necessary when a new eden region is
  // allocated and update any jstat counters that need to be updated.
  void update_eden_size();

  CollectorCounters* incremental_collection_counters() {
    return _incremental_collection_counters;
  }
  CollectorCounters* full_collection_counters() {
    return _full_collection_counters;
  }
  CollectorCounters* conc_collection_counters() {
    return _conc_collection_counters;
  }
  GenerationCounters* young_collection_counters() {
    return _young_collection_counters;
  }
  GenerationCounters* old_collection_counters() {
    return _old_collection_counters;
  }
  HSpaceCounters*      old_space_counters() { return _old_space_counters; }
  HSpaceCounters*      eden_counters() { return _eden_counters; }
  HSpaceCounters*      from_counters() { return _from_counters; }
  HSpaceCounters*      to_counters() { return _to_counters; }

  // Monitoring support used by
  //   MemoryService
  //   jstat counters
  //   Tracing

  size_t overall_reserved()           { return _overall_reserved;     }
  size_t overall_committed()          { return _overall_committed;    }
  size_t overall_used()               { return _overall_used;         }

  size_t young_gen_committed()        { return _young_gen_committed;  }
  size_t young_gen_max();
  size_t eden_space_committed()       { return _eden_committed;       }
  size_t eden_space_used()            { return _eden_used;            }
  size_t survivor_space_committed()   { return _survivor_committed;   }
  size_t survivor_space_used()        { return _survivor_used;        }

  size_t old_gen_committed()          { return old_space_committed(); }
  size_t old_gen_max();
  size_t old_space_committed()        { return _old_committed;        }
  size_t old_space_used()             { return _old_used;             }
};

class G1GenerationCounters: public GenerationCounters {
protected:
  G1MonitoringSupport* _g1mm;

public:
  G1GenerationCounters(G1MonitoringSupport* g1mm,
                       const char* name, int ordinal, int spaces,
                       size_t min_capacity, size_t max_capacity,
                       size_t curr_capacity);
};

class G1YoungGenerationCounters: public G1GenerationCounters {
public:
  G1YoungGenerationCounters(G1MonitoringSupport* g1mm, const char* name);
  virtual void update_all();
};

class G1OldGenerationCounters: public G1GenerationCounters {
public:
  G1OldGenerationCounters(G1MonitoringSupport* g1mm, const char* name);
  virtual void update_all();
};

#endif // SHARE_VM_GC_G1_G1MONITORINGSUPPORT_HPP
