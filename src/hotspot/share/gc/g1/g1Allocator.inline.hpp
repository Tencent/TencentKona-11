/*
 * Copyright (c) 2015, 2018, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_VM_GC_G1_G1ALLOCATOR_INLINE_HPP
#define SHARE_VM_GC_G1_G1ALLOCATOR_INLINE_HPP

#include "gc/g1/g1Allocator.hpp"
#include "gc/g1/g1AllocRegion.inline.hpp"
#include "gc/shared/plab.inline.hpp"

inline uint G1Allocator::current_node_index() const {
  return _numa->index_of_current_thread();
}

inline MutatorAllocRegion* G1Allocator::mutator_alloc_region(uint node_index) {
  assert(node_index < _num_alloc_regions, "Invalid index: %u", node_index);
  return &_mutator_alloc_regions[node_index];
}

inline SurvivorGCAllocRegion* G1Allocator::survivor_gc_alloc_region(uint node_index) {
  assert(node_index < _num_alloc_regions, "Invalid index: %u", node_index);
  return &_survivor_gc_alloc_regions[node_index];
}

inline OldGCAllocRegion* G1Allocator::old_gc_alloc_region() {
  return &_old_gc_alloc_region;
}

inline HeapWord* G1Allocator::attempt_allocation(size_t min_word_size,
                                                 size_t desired_word_size,
                                                 size_t* actual_word_size) {
  uint node_index = current_node_index();
  HeapWord* result = mutator_alloc_region(node_index)->attempt_retained_allocation(min_word_size, desired_word_size, actual_word_size);
  if (result != NULL) {
    return result;
  }
  return mutator_alloc_region(node_index)->attempt_allocation(min_word_size, desired_word_size, actual_word_size);
}

inline HeapWord* G1Allocator::attempt_allocation_locked(size_t word_size) {
  uint node_index = current_node_index();
  HeapWord* result = mutator_alloc_region(node_index)->attempt_allocation_locked(word_size);
  assert(result != NULL || mutator_alloc_region(node_index)->get() == NULL,
         "Must not have a mutator alloc region if there is no memory, but is " PTR_FORMAT, p2i(mutator_alloc_region(node_index)->get()));
  return result;
}

inline HeapWord* G1Allocator::attempt_allocation_force(size_t word_size) {
  uint node_index = current_node_index();
  return mutator_alloc_region(node_index)->attempt_allocation_force(word_size);
}

inline PLAB* G1PLABAllocator::alloc_buffer(InCSetState dest, uint node_index) const {
  assert(dest.is_valid(),
         "Allocation buffer index out of bounds: %s", dest.get_type_str());
  assert(_alloc_buffers[dest.value()] != NULL,
         "Allocation buffer is NULL: %s", dest.get_type_str());
  return alloc_buffer(dest.value(), node_index);
}

inline PLAB* G1PLABAllocator::alloc_buffer(in_cset_state_t dest, uint node_index) const {
  assert(dest < InCSetState::Num,
         "Allocation buffer index out of bounds: %u", dest);

  if (dest == InCSetState::Young) {
    assert(node_index < alloc_buffers_length(dest),
           "Allocation buffer index out of bounds: %u, %u", dest, node_index);
    return _alloc_buffers[dest][node_index];
  } else {
    return _alloc_buffers[dest][0];
  }
}

inline uint G1PLABAllocator::alloc_buffers_length(in_cset_state_t dest) const {
  if (dest == InCSetState::Young) {
    return _allocator->num_nodes();
  } else {
    return 1;
  }
}

inline HeapWord* G1PLABAllocator::plab_allocate(InCSetState dest,
                                                size_t word_sz,
                                                uint node_index) {
  PLAB* buffer = alloc_buffer(dest, node_index);
  if (_survivor_alignment_bytes == 0 || !dest.is_young()) {
    return buffer->allocate(word_sz);
  } else {
    return buffer->allocate_aligned(word_sz, _survivor_alignment_bytes);
  }
}

inline HeapWord* G1PLABAllocator::allocate(InCSetState dest,
                                           size_t word_sz,
                                           bool* refill_failed,
                                           uint node_index) {
  HeapWord* const obj = plab_allocate(dest, word_sz, node_index);
  if (obj != NULL) {
    return obj;
  }
  return allocate_direct_or_new_plab(dest, word_sz, refill_failed, node_index);
}

// Create the maps which is used to identify archive objects.
inline void G1ArchiveAllocator::enable_archive_object_check() {
  if (_archive_check_enabled) {
    return;
  }

  _archive_check_enabled = true;
  size_t length = Universe::heap()->max_capacity();
  _closed_archive_region_map.initialize((HeapWord*)Universe::heap()->base(),
                                        (HeapWord*)Universe::heap()->base() + length,
                                        HeapRegion::GrainBytes);
  _open_archive_region_map.initialize((HeapWord*)Universe::heap()->base(),
                                      (HeapWord*)Universe::heap()->base() + length,
                                      HeapRegion::GrainBytes);
}

// Set the regions containing the specified address range as archive.
inline void G1ArchiveAllocator::set_range_archive(MemRegion range, bool open) {
  assert(_archive_check_enabled, "archive range check not enabled");
  log_info(gc, cds)("Mark %s archive regions in map: [" PTR_FORMAT ", " PTR_FORMAT "]",
                     open ? "open" : "closed",
                     p2i(range.start()),
                     p2i(range.last()));
  if (open) {
    _open_archive_region_map.set_by_address(range, true);
  } else {
    _closed_archive_region_map.set_by_address(range, true);
  }
}

// Clear the archive regions map containing the specified address range.
inline void G1ArchiveAllocator::clear_range_archive(MemRegion range, bool open) {
  assert(_archive_check_enabled, "archive range check not enabled");
  log_info(gc, cds)("Clear %s archive regions in map: [" PTR_FORMAT ", " PTR_FORMAT "]",
                    open ? "open" : "closed",
                    p2i(range.start()),
                    p2i(range.last()));
  if (open) {
    _open_archive_region_map.set_by_address(range, false);
  } else {
    _closed_archive_region_map.set_by_address(range, false);
  }
}

// Check if an object is in a closed archive region using the _archive_region_map.
inline bool G1ArchiveAllocator::in_closed_archive_range(oop object) {
  // This is the out-of-line part of is_closed_archive_object test, done separately
  // to avoid additional performance impact when the check is not enabled.
  return _closed_archive_region_map.get_by_address((HeapWord*)object);
}

inline bool G1ArchiveAllocator::in_open_archive_range(oop object) {
  return _open_archive_region_map.get_by_address((HeapWord*)object);
}

// Check if archive object checking is enabled, to avoid calling in_open/closed_archive_range
// unnecessarily.
inline bool G1ArchiveAllocator::archive_check_enabled() {
  return _archive_check_enabled;
}

inline bool G1ArchiveAllocator::is_closed_archive_object(oop object) {
  return (archive_check_enabled() && in_closed_archive_range(object));
}

inline bool G1ArchiveAllocator::is_open_archive_object(oop object) {
  return (archive_check_enabled() && in_open_archive_range(object));
}

inline bool G1ArchiveAllocator::is_archive_object(oop object) {
  return (archive_check_enabled() && (in_closed_archive_range(object) ||
                                      in_open_archive_range(object)));
}

#endif // SHARE_VM_GC_G1_G1ALLOCATOR_HPP
