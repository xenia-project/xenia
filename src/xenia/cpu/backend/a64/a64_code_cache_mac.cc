// a64_code_cache_macos.cc
/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Ben Vanik. All rights reserved.
 * Released under the BSD license - see LICENSE in the root for more details.
 ******************************************************************************
 */

#include "xenia/cpu/backend/a64/a64_code_cache.h"

#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
#include <libunwind.h>
#include <libkern/OSCacheControl.h>

#include "xenia/base/assert.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/cpu/function.h"

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

// ARM64 unwind-op codes for MacOS (DWARF-based)
typedef enum _UNWIND_OP_CODES_MACHO {
  UWOP_MACHO_NOP = 0x00,
  UWOP_MACHO_ALLOC_STACK = 0x01,  // DWARF CFI: adjust stack pointer
  UWOP_MACHO_SAVE_FP_LR = 0x02,    // DWARF CFI: save frame pointer and link register
  UWOP_MACHO_SET_FP = 0x03,        // DWARF CFI: set frame pointer
  UWOP_MACHO_END = 0xFF,
} UNWIND_CODE_OPS_MACHO;

using UNWIND_CODE_MACHO = uint8_t;

// Size of unwind info per function.
static const size_t kUnwindInfoSize = 16;  // Example size, adjust as needed

class MacOSA64CodeCache : public A64CodeCache {
 public:
  MacOSA64CodeCache();
  ~MacOSA64CodeCache() override;

  bool Initialize() override;

  void* LookupUnwindInfo(uint64_t host_pc) override;

 private:
  struct UnwindInfo {
    uint64_t begin_address;
    uint64_t end_address;
    // Additional unwind information can be added here
  };

  UnwindReservation RequestUnwindReservation(uint8_t* entry_address) override;
  void PlaceCode(uint32_t guest_address, void* machine_code,
                const EmitFunctionInfo& func_info, void* code_execute_address,
                UnwindReservation unwind_reservation) override;

  void InitializeUnwindEntry(uint8_t* unwind_entry_address,
                             size_t unwind_table_slot,
                             void* code_execute_address,
                             const EmitFunctionInfo& func_info);

  // Unwind table entries.
  std::vector<UnwindInfo> unwind_table_;
  // Current number of entries in the table.
  std::atomic<uint32_t> unwind_table_count_ = {0};
};

std::unique_ptr<A64CodeCache> A64CodeCache::Create() {
  return std::make_unique<MacOSA64CodeCache>();
}

MacOSA64CodeCache::MacOSA64CodeCache() = default;

MacOSA64CodeCache::~MacOSA64CodeCache() {
  // Cleanup if necessary
}

bool MacOSA64CodeCache::Initialize() {
  if (!A64CodeCache::Initialize()) {
    return false;
  }

  // Reserve space for unwind table entries
  unwind_table_.reserve(kMaximumFunctionCount);

  // Additional MacOS-specific initialization can be done here

  return true;
}

MacOSA64CodeCache::UnwindReservation
MacOSA64CodeCache::RequestUnwindReservation(uint8_t* entry_address) {
  assert_false(unwind_table_count_ >= kMaximumFunctionCount);
  UnwindReservation unwind_reservation;
  unwind_reservation.data_size = xe::round_up(kUnwindInfoSize, 16);
  unwind_reservation.table_slot = unwind_table_count_++;
  unwind_reservation.entry_address = entry_address;
  return unwind_reservation;
}

void MacOSA64CodeCache::PlaceCode(uint32_t guest_address, void* machine_code,
                                  const EmitFunctionInfo& func_info,
                                  void* code_execute_address,
                                  UnwindReservation unwind_reservation) {
  // Add unwind info.
  InitializeUnwindEntry(reinterpret_cast<uint8_t*>(unwind_reservation.entry_address),
                        unwind_reservation.table_slot, code_execute_address,
                        func_info);

  // Add entry to unwind table.
  UnwindInfo unwind_info;
  unwind_info.begin_address = reinterpret_cast<uintptr_t>(code_execute_address);
  unwind_info.end_address = unwind_info.begin_address + func_info.code_size.total;
  unwind_table_.emplace_back(unwind_info);

    // Flush instruction cache to ensure code is executable
    sys_icache_invalidate(code_execute_address, func_info.code_size.total);
    // Since sys_icache_invalidate returns void, we cannot check its return value.
    // If you need to log or handle errors, ensure that the addresses and sizes are correct.
}

void MacOSA64CodeCache::InitializeUnwindEntry(
    uint8_t* unwind_entry_address, size_t unwind_table_slot,
    void* code_execute_address, const EmitFunctionInfo& func_info) {
  // Initialize unwind information for MacOS (simplified example)
  // In practice, you would populate this with proper DWARF unwind info
  // based on the function prologue and epilogue.

  // Example: Setting up a basic frame pointer and link register save
  UnwindInfo unwind_info;
  unwind_info.begin_address = reinterpret_cast<uintptr_t>(code_execute_address);
  unwind_info.end_address = unwind_info.begin_address + func_info.code_size.total;

  // Store unwind info in the table
  unwind_table_[unwind_table_slot] = unwind_info;
}

void* MacOSA64CodeCache::LookupUnwindInfo(uint64_t host_pc) {
  // Binary search the unwind table for the given program counter
  size_t left = 0;
  size_t right = unwind_table_count_.load();
  while (left < right) {
    size_t mid = left + (right - left) / 2;
    const UnwindInfo& info = unwind_table_[mid];
    if (host_pc < info.begin_address) {
      right = mid;
    } else if (host_pc >= info.end_address) {
      left = mid + 1;
    } else {
      return &unwind_table_[mid];
    }
  }
  return nullptr;
}

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
