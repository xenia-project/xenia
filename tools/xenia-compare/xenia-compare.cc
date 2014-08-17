/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <memory>

#include <gflags/gflags.h>
#include <poly/main.h>
#include <poly/poly.h>
#include <xdb/postmortem_debug_target.h>
#include <xdb/xdb.h>

DEFINE_string(trace_file_left, "", "Trace file to compare (original).");
DEFINE_string(trace_file_right, "", "Trace file to compare (new).");

namespace xc {

using xdb::PostmortemDebugTarget;

int main(std::vector<std::wstring>& args) {
  auto left_target = std::make_unique<PostmortemDebugTarget>();
  if (!left_target->LoadTrace(poly::to_wstring(FLAGS_trace_file_left))) {
    XEFATAL("Unable to load left trace file: %s",
            FLAGS_trace_file_left.c_str());
  }
  auto right_target = std::make_unique<PostmortemDebugTarget>();
  if (!right_target->LoadTrace(poly::to_wstring(FLAGS_trace_file_right))) {
    XEFATAL("Unable to load right trace file: %s",
            FLAGS_trace_file_right.c_str());
  }

  return 0;
}

}  // namespace xc

DEFINE_ENTRY_POINT(L"xenia-compare", L"xenia-compare", xc::main);
