/*
 *
 * Copyright (C) 2021 THL A29 Limited, a Tencent company. All rights reserved.
 * DO NOT ALTER OR REMOVE NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation. THL A29 Limited designates
 * this particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License version 2 for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include "precompiled.hpp"
#include "runtime/coroutine.hpp"
#include "runtime/execution_unit.hpp"
#include "runtime/threadSMR.inline.hpp"
#include "classfile/systemDictionary.hpp"
#include "classfile/javaClasses.hpp"

ExecutionType* ExecutionUnit::get_execution_unit(oop threadObj) {
  assert(threadObj != NULL, "should not null");
#if INCLUDE_KONA_FIBER
  // threadObj could be j.l.Thread or VT, return coroutine
  if (threadObj->klass() == SystemDictionary::VT_klass()) {
    // get _continuation and coroutine
    oop cont = java_lang_VT::Cont(threadObj);
    if (cont == NULL) {
      return NULL;
    }
    return (ExecutionType*)java_lang_Continuation::data(cont);
  } else {
    JavaThread* t = java_lang_Thread::thread(threadObj);
    return t->thread_coroutine();
  }
#else
  return java_lang_Thread::thread(threadObj);
#endif
}

ExecutionType* ExecutionUnit::owning_thread_from_monitor_owner(ThreadsList* t_list, address owner) {
#if INCLUDE_KONA_FIBER
  if (YieldWithMonitor) {
    return Coroutine::owning_coro_from_monitor_owner(owner);
  } else {
    JavaThread* t = Threads::owning_thread_from_monitor_owner(t_list, owner);
    if (t == NULL) {
      return NULL;
    } else {
      return t->current_coroutine();
    }
  }
#else
  return Threads::owning_thread_from_monitor_owner(t_list, owner);
#endif
}

ExecutionUnitsIterator::ExecutionUnitsIterator(ThreadsList* t_list) :
#if INCLUDE_KONA_FIBER
  _cur(NULL)
#else
  _list(t_list),
  _index(0)
#endif
{
}

ExecutionType* ExecutionUnitsIterator::first() {
#if INCLUDE_KONA_FIBER
  for (_cur_bucket_index = 0; _cur_bucket_index < CONT_CONTAINER_SIZE; _cur_bucket_index++) {
    ContBucket* bucket = ContContainer::bucket(_cur_bucket_index);
    _cur = bucket->head();
    if (_cur != NULL) {
      return _cur;
    }
  }
  _cur = NULL;
  return NULL;
#else
  if (_list->length() == 0) {
    return NULL;
  }
  _index = 1;
  return _list->thread_at(0);
#endif
}

ExecutionType* ExecutionUnitsIterator::next() {
#if INCLUDE_KONA_FIBER
  if (_cur == NULL) {
    return NULL;
  }
  ContBucket* bucket = ContContainer::bucket(_cur_bucket_index);
  _cur = _cur->next();
  if (_cur == bucket->head()) {
    _cur_bucket_index++;
    for (; _cur_bucket_index < CONT_CONTAINER_SIZE; _cur_bucket_index++) {
      bucket = ContContainer::bucket(_cur_bucket_index);
      _cur = bucket->head();
      if (_cur != NULL) {
        return _cur;
      }
    }
    _cur = NULL;
    return NULL;
  } else {
    return _cur;
  }
#else
  if (_index >= _list->length()) {
    return NULL;
  }
  return _list->thread_at(_index++);
#endif
}
