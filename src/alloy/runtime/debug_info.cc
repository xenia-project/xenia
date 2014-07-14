/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/runtime/debug_info.h>

namespace alloy {
namespace runtime {

DebugInfo::DebugInfo()
    : source_disasm_(nullptr),
      raw_hir_disasm_(nullptr),
      hir_disasm_(nullptr),
      machine_code_disasm_(nullptr),
      source_map_count_(0),
      source_map_(nullptr) {}

DebugInfo::~DebugInfo() {
  xe_free(source_map_);
  xe_free(source_disasm_);
  xe_free(raw_hir_disasm_);
  xe_free(hir_disasm_);
  xe_free(machine_code_disasm_);
}

void DebugInfo::InitializeSourceMap(size_t source_map_count,
                                    SourceMapEntry* source_map) {
  source_map_count_ = source_map_count;
  source_map_ = source_map;

  // TODO(benvanik): ensure sorted in some way? MC offset?
}

SourceMapEntry* DebugInfo::LookupSourceOffset(uint64_t offset) {
  // TODO(benvanik): binary search? We know the list is sorted by code order.
  for (size_t n = 0; n < source_map_count_; n++) {
    auto entry = &source_map_[n];
    if (entry->source_offset == offset) {
      return entry;
    }
  }
  return nullptr;
}

SourceMapEntry* DebugInfo::LookupHIROffset(uint64_t offset) {
  // TODO(benvanik): binary search? We know the list is sorted by code order.
  for (size_t n = 0; n < source_map_count_; n++) {
    auto entry = &source_map_[n];
    if (entry->hir_offset >= offset) {
      return entry;
    }
  }
  return nullptr;
}

SourceMapEntry* DebugInfo::LookupCodeOffset(uint64_t offset) {
  // TODO(benvanik): binary search? We know the list is sorted by code order.
  for (int64_t n = source_map_count_ - 1; n >= 0; n--) {
    auto entry = &source_map_[n];
    if (entry->code_offset <= offset) {
      return entry;
    }
  }
  return nullptr;
}

}  // namespace runtime
}  // namespace alloy
