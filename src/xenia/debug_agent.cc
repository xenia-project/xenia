/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/debug_agent.h>

#include <poly/string.h>
#include <gflags/gflags.h>

DEFINE_string(trace_file, "", "Trace to the given file.");
DEFINE_uint64(trace_capacity, 0x40000000, "Trace file capacity to allocate.");

namespace xe {

DebugAgent::DebugAgent(Emulator* emulator)
    : emulator_(emulator),
      file_(nullptr),
      file_mapping_(nullptr),
      trace_base_(0) {}

DebugAgent::~DebugAgent() {
  if (trace_base_) {
    UnmapViewOfFile(trace_base_);
  }

  CloseHandle(file_mapping_);
  CloseHandle(file_);
}

int DebugAgent::Initialize() {
  if (!FLAGS_trace_file.empty()) {
    if (SetupTracing(FLAGS_trace_file, FLAGS_trace_capacity)) {
      XELOGE("Failed to setup tracing - out of disk space?");
      return 1;
    }
  }

  return 0;
}

int DebugAgent::SetupTracing(const std::string& trace_file, uint64_t capacity) {
  auto file_path = poly::to_wstring(trace_file);
  file_ = CreateFile(file_path.c_str(), GENERIC_READ | GENERIC_WRITE,
                     FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS,
                     FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_TEMPORARY,
                     nullptr);
  if (!file_) {
    XELOGE("Could not open trace file for writing");
    return 1;
  }

  file_mapping_ = CreateFileMapping(
      file_, nullptr, PAGE_READWRITE, static_cast<uint32_t>(capacity >> 32),
      static_cast<uint32_t>(capacity), L"Local\\xenia_xdb_trace");
  if (!file_mapping_) {
    XELOGE("Could not create trace file mapping - out of disk space?");
    return 1;
  }

  trace_base_ = MapViewOfFile(file_mapping_, FILE_MAP_WRITE, 0, 0, 0);
  if (!trace_base_) {
    XELOGE("Could not map view of trace file - out of disk space?");
    return 1;
  }

  // Store initial trace base.
  poly::store<uint64_t>(trace_base_, trace_base() + 8);

  return 0;
}

}  // namespace xe
