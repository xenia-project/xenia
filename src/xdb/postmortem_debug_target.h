/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XDB_POSTMORTEM_DEBUG_TARGET_H_
#define XDB_POSTMORTEM_DEBUG_TARGET_H_

#include <atomic>
#include <string>

#include <xdb/debug_target.h>

namespace xdb {

class PostmortemDebugTarget : public DebugTarget {
 public:
  PostmortemDebugTarget() = default;

  bool LoadTrace(const std::wstring& path);
  bool LoadContent(const std::wstring& path = L"");

  bool Prepare();
  bool Prepare(std::atomic<bool>& cancelled);
};

}  // namespace xdb

#endif  // XDB_POSTMORTEM_DEBUG_TARGET_H_
