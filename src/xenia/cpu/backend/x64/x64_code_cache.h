/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BACKEND_X64_X64_CODE_CACHE_H_
#define XENIA_BACKEND_X64_X64_CODE_CACHE_H_

// For RUNTIME_FUNCTION:
#include "xenia/base/platform.h"

#include <atomic>
#include <mutex>
#include <vector>

#include "xenia/base/mutex.h"

namespace xe {
namespace cpu {
namespace backend {
namespace x64 {

class X64CodeCache {
 public:
  X64CodeCache();
  virtual ~X64CodeCache();

  bool Initialize();

  // TODO(benvanik): ELF serialization/etc
  // TODO(benvanik): keep track of code blocks
  // TODO(benvanik): padding/guards/etc

  void set_indirection_default(uint32_t default_value);
  void AddIndirection(uint32_t guest_address, uint32_t host_address);

  void CommitExecutableRange(uint32_t guest_low, uint32_t guest_high);

  void* PlaceCode(uint32_t guest_address, void* machine_code, size_t code_size,
                  size_t stack_size);

  uint32_t PlaceData(const void* data, size_t length);

 private:
  const static uint64_t kIndirectionTableBase = 0x80000000;
  const static uint64_t kIndirectionTableSize = 0x1FFFFFFF;
  const static uint64_t kGeneratedCodeBase = 0xA0000000;
  const static uint64_t kGeneratedCodeSize = 0x0FFFFFFF;

  void InitializeUnwindEntry(uint8_t* unwind_entry_address,
                             size_t unwind_table_slot, uint8_t* code_address,
                             size_t code_size, size_t stack_size);

  // Must be held when manipulating the offsets or counts of anything, to keep
  // the tables consistent and ordered.
  xe::mutex allocation_mutex_;

  // Value that the indirection table will be initialized with upon commit.
  uint32_t indirection_default_value_;

  // Fixed at kIndirectionTableBase in host space, holding 4 byte pointers into
  // the generated code table that correspond to the PPC functions in guest
  // space.
  uint8_t* indirection_table_base_;
  // Fixed at kGeneratedCodeBase and holding all generated code, growing as
  // needed.
  uint8_t* generated_code_base_;
  // Current offset to empty space in generated code.
  size_t generated_code_offset_;
  // Current high water mark of COMMITTED code.
  std::atomic<size_t> generated_code_commit_mark_;

  // Growable function table system handle.
  void* unwind_table_handle_;
  // Actual unwind table entries.
  std::vector<RUNTIME_FUNCTION> unwind_table_;
  // Current number of entries in the table.
  std::atomic<uint32_t> unwind_table_count_;
};

}  // namespace x64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_BACKEND_X64_X64_CODE_CACHE_H_
