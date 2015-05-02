/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XDB_CURSOR_H_
#define XDB_CURSOR_H_

#include <functional>

#include "xenia/base/delegate.h"
#include <xdb/module.h>
#include <xdb/protocol.h>
#include <xdb/thread.h>

namespace xdb {

class Cursor {
 public:
  virtual ~Cursor() = default;

  virtual bool can_step() const = 0;

  // TODO(benvanik): breakpoints/events

  xe::Delegate<void> end_of_stream;

  std::vector<Module*> modules();
  std::vector<Thread*> threads();

  xe::Delegate<Module*> module_loaded;
  xe::Delegate<Module*> module_unloaded;
  xe::Delegate<Thread*> thread_created;
  xe::Delegate<Thread*> thread_exited;

  // TODO(benvanik): memory access

  virtual void Continue(Thread* thread = nullptr) = 0;
  virtual void ContinueTo(uint32_t address, Thread* thread = nullptr) = 0;
  virtual void ContinueToNextCall(Thread* thread = nullptr) = 0;
  virtual void ContinueToReturn(Thread* thread = nullptr) = 0;
  virtual void ContinueUntil(
      const std::function<bool(protocol::EventType, uint8_t*)> predicate,
      Thread* thread = nullptr) = 0;
  virtual void Break() = 0;

 protected:
  Cursor() = default;
};

}  // namespace xdb

#endif  // XDB_CURSOR_H_
