/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.
 * Released under the BSD license - see LICENSE in the root for more details.
 ******************************************************************************
 */

#include "xenia/cpu/backend/a64/a64_code_cache.h"

#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

#ifdef XE_PLATFORM_MAC
#include <libkern/OSCacheControl.h>
#include <pthread.h>
#endif

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

// ARM64 unwind-op codes for POSIX (simplified)
typedef enum _UNWIND_OP_CODES_POSIX {
  UWOP_POSIX_NOP = 0x00,
  UWOP_POSIX_ALLOC_STACK = 0x01,
  UWOP_POSIX_SAVE_FP_LR = 0x02,
  UWOP_POSIX_SET_FP = 0x03,
  UWOP_POSIX_END = 0xFF,
} UNWIND_CODE_OPS_POSIX;

using UNWIND_CODE_POSIX = uint8_t;

// Size of unwind info per function.
static const size_t kUnwindInfoSize = 16;

class PosixA64CodeCache : public A64CodeCache {
 public:
  PosixA64CodeCache();
  ~PosixA64CodeCache() override;

  bool Initialize() override;

  void* LookupUnwindInfo(uint64_t host_pc) override;

 protected:
  void CopyMachineCode(void* dest, const void* src, size_t size) override;

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
  return std::make_unique<PosixA64CodeCache>();
}

PosixA64CodeCache::PosixA64CodeCache() = default;

PosixA64CodeCache::~PosixA64CodeCache() {
  // Cleanup if necessary
}

bool PosixA64CodeCache::Initialize() {
  if (!A64CodeCache::Initialize()) {
    return false;
  }

  // Resize (not reserve) space for unwind table entries to ensure vector has actual elements
  unwind_table_.resize(kMaximumFunctionCount);

  // Additional POSIX-specific initialization can be done here

  return true;
}

void PosixA64CodeCache::CopyMachineCode(void* dest, const void* src, size_t size) {
#ifdef XE_PLATFORM_MAC
  // On ARM64 macOS with MAP_JIT, use pthread_jit_write_protect_np dance
  
  // Enable write access for this thread (disable execute)
  pthread_jit_write_protect_np(0);
  
  // Copy the machine code
  std::memcpy(dest, src, size);
  
  // Re-enable execute access for this thread (disable write)
  pthread_jit_write_protect_np(1);
#else
  // On Linux and other POSIX systems, just copy directly
  // The memory should already be writable if allocated properly
  std::memcpy(dest, src, size);
#endif
}

PosixA64CodeCache::UnwindReservation
PosixA64CodeCache::RequestUnwindReservation(uint8_t* entry_address) {
  uint32_t current_count = unwind_table_count_.fetch_add(1);
  assert_false(current_count >= kMaximumFunctionCount);
  UnwindReservation unwind_reservation;
  unwind_reservation.data_size = xe::round_up(kUnwindInfoSize, 16);
  unwind_reservation.table_slot = current_count;
  unwind_reservation.entry_address = entry_address;
  return unwind_reservation;
}

void PosixA64CodeCache::PlaceCode(uint32_t guest_address, void* machine_code,
                                  const EmitFunctionInfo& func_info,
                                  void* code_execute_address,
                                  UnwindReservation unwind_reservation) {
  // Add unwind info.
  InitializeUnwindEntry(reinterpret_cast<uint8_t*>(unwind_reservation.entry_address),
                        unwind_reservation.table_slot, code_execute_address,
                        func_info);

  // Add entry to unwind table at the reserved slot only
  UnwindInfo unwind_info;
  unwind_info.begin_address = reinterpret_cast<uintptr_t>(code_execute_address);
  unwind_info.end_address = unwind_info.begin_address + func_info.code_size.total;
  
  // Store in the reserved slot
  unwind_table_[unwind_reservation.table_slot] = unwind_info;
  
  // Validate address alignment before cache flushing
  if ((uintptr_t)code_execute_address % 4 != 0) {
    XELOGW("PosixA64CodeCache::PlaceCode: WARNING - code address 0x{:016X} is not 4-byte aligned", 
           (uintptr_t)code_execute_address);
  }
  
  if (func_info.code_size.total % 4 != 0) {
    XELOGW("PosixA64CodeCache::PlaceCode: WARNING - code size {} is not 4-byte aligned", 
           func_info.code_size.total);
  }
  
  // Flush instruction cache
#ifdef XE_PLATFORM_MAC
  // On macOS, use sys_icache_invalidate
  sys_icache_invalidate(code_execute_address, func_info.code_size.total);
#else
  // On Linux and other POSIX systems, use GCC builtin
  __builtin___clear_cache(static_cast<char*>(code_execute_address),
                          static_cast<char*>(code_execute_address) + func_info.code_size.total);
#endif
}

void PosixA64CodeCache::InitializeUnwindEntry(
    uint8_t* unwind_entry_address, size_t unwind_table_slot,
    void* code_execute_address, const EmitFunctionInfo& func_info) {
  // Initialize unwind information for POSIX (simplified example)
  // In practice, you would populate this with proper unwind info
  // based on the function prologue and epilogue.

  // NOTE: Unwind info is already stored in PlaceCode, so we don't store it again here
  // to avoid the double-storage bug that was causing memory corruption.
}

void* PosixA64CodeCache::LookupUnwindInfo(uint64_t host_pc) {
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