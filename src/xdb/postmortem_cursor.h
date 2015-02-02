/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XDB_POSTMORTEM_CURSOR_H_
#define XDB_POSTMORTEM_CURSOR_H_

#include <memory>

#include <xdb/cursor.h>

namespace xdb {

class PostmortemDebugTarget;

class PostmortemCursor : public Cursor {
 public:
  PostmortemCursor(PostmortemDebugTarget* target);
  ~PostmortemCursor() override;

  bool can_step() const override;

  void Continue(Thread* thread = nullptr) override;
  void ContinueTo(uint32_t address, Thread* thread = nullptr) override;
  void ContinueToNextCall(Thread* thread = nullptr) override;
  void ContinueToReturn(Thread* thread = nullptr) override;
  void ContinueUntil(
      const std::function<bool(protocol::EventType, uint8_t*)> predicate,
      Thread* thread = nullptr) override;
  void Break() override;

 private:
  class Executor;

  PostmortemDebugTarget* target_;
  std::unique_ptr<Executor> executor_;
};

}  // namespace xdb

#endif  // XDB_POSTMORTEM_CURSOR_H_
