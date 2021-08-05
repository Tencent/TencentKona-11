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
 */

#include "precompiled.hpp"
#include "gc/z/zAddress.inline.hpp"
#include "gc/z/zBarrier.inline.hpp"
#include "gc/z/zHeap.hpp"
#include "gc/z/zOopClosures.inline.hpp"
#include "gc/z/zPage.hpp"
#include "gc/z/zRelocate.hpp"
#include "gc/z/zRelocationSet.inline.hpp"
#include "gc/z/zRootsIterator.hpp"
#include "gc/z/zTask.hpp"
#include "gc/z/zWorkers.hpp"
#if INCLUDE_KONA_FIBER
#include "runtime/coroutine.hpp"
#endif

ZRelocate::ZRelocate(ZWorkers* workers) :
    _workers(workers) {}

class ZRelocateRootsIteratorClosure : public ZRootsIteratorClosure {
private:
  static void remap_address(HeapWord** p) {
    *p = (HeapWord*)ZAddress::good_or_null((uintptr_t)*p);
  }

public:
  virtual void do_thread(Thread* thread) {
    ZRootsIteratorClosure::do_thread(thread);

    // Update thread local address bad mask
    ZThreadLocalData::set_address_bad_mask(thread, ZAddressBadMask);

    // Remap TLAB
    if (UseTLAB && thread->is_Java_thread()) {
      thread->tlab().addresses_do(remap_address);
    }
  }

  virtual void do_oop(oop* p) {
    ZBarrier::relocate_barrier_on_root_oop_field(p);
  }

  virtual void do_oop(narrowOop* p) {
    ShouldNotReachHere();
  }

#if INCLUDE_KONA_FIBER
  virtual bool should_do_coroutine() {
    return false;
  }
#endif
};

class ZRelocateRootsTask : public ZTask {
private:
  ZRootsIterator                _roots;
  ZRelocateRootsIteratorClosure _cl;

public:
  ZRelocateRootsTask() :
      ZTask("ZRelocateRootsTask"),
      _roots() {}

  virtual void work() {
    // During relocation we need to visit the JVMTI
    // export weak roots to rehash the JVMTI tag map
    _roots.oops_do(&_cl, true /* visit_jvmti_weak_export */);
  }
};

void ZRelocate::start() {
  // ZRootsIterator destruct before Coroutine::start_concurrent
  {
    ZRelocateRootsTask task;
    _workers->run_parallel(&task);
  }
#if INCLUDE_KONA_FIBER
  if (UseKonaFiber) {
    Coroutine::start_concurrent(Coroutine::_ZConcurrent);
  }
#endif
}

class ZRelocateObjectClosure : public ObjectClosure {
private:
  ZPage* const _page;

public:
  ZRelocateObjectClosure(ZPage* page) :
      _page(page) {}

  virtual void do_object(oop o) {
    _page->relocate_object(ZOop::to_address(o));
  }
};

bool ZRelocate::work(ZRelocationSetParallelIterator* iter) {
  bool success = true;

  // Relocate pages in the relocation set
  for (ZPage* page; iter->next(&page);) {
    // Relocate objects in page
    ZRelocateObjectClosure cl(page);
    page->object_iterate(&cl);

    if (ZVerifyForwarding) {
      page->verify_forwarding();
    }

    if (page->is_pinned()) {
      // Relocation failed, page is now pinned
      success = false;
    } else {
      // Relocation succeeded, release page
      ZHeap::heap()->release_page(page, true /* reclaimed */);
    }
  }

  return success;
}

class ZRelocateTask : public ZTask {
private:
  ZRelocate* const               _relocate;
  ZRelocationSetParallelIterator _iter;
  bool                           _failed;

public:
  ZRelocateTask(ZRelocate* relocate, ZRelocationSet* relocation_set) :
      ZTask("ZRelocateTask"),
      _relocate(relocate),
      _iter(relocation_set),
      _failed(false) {}

  virtual void work() {
    if (!_relocate->work(&_iter)) {
      _failed = true;
    }
  }

  bool failed() const {
    return _failed;
  }
};

bool ZRelocate::relocate(ZRelocationSet* relocation_set) {
#if INCLUDE_KONA_FIBER
  if (UseKonaFiber) {
    ZLoadBarrierOopClosure cl;
    ZConcurrentCoroutineTask task(&cl);
    _workers->run_concurrent(&task);
    Coroutine::end_concurrent();
  }
#endif
  ZRelocateTask task(this, relocation_set);
  _workers->run_concurrent(&task);
  return !task.failed();
}
