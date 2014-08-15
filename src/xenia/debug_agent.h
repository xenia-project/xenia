/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_AGENT_H_
#define XENIA_DEBUG_AGENT_H_

#include <string>

#include <xenia/core.h>

namespace xe {

class Emulator;

class DebugAgent {
 public:
  DebugAgent(Emulator* emulator);
  ~DebugAgent();

  uint64_t trace_base() const {
    return reinterpret_cast<uint64_t>(trace_base_);
  }

  int Initialize();

 private:
  int SetupTracing(const std::string& trace_file, uint64_t capacity);

  Emulator* emulator_;

  HANDLE file_;
  HANDLE file_mapping_;
  void* trace_base_;
};

}  // namespace xe

#endif  // XENIA_DEBUG_AGENT_H_
