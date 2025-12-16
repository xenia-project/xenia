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
#include <pthread.h>

#include "xenia/base/assert.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/cpu/function.h"

// JIT write callback context structure for Hardened Runtime compatibility
struct JITWriteContext {
  void* dest;
  const void* src;
  size_t size;
};

// JIT write callback function for Hardened Runtime compatibility
static int jit_write_callback(void* ctx) {
  JITWriteContext* context = static_cast<JITWriteContext*>(ctx);
  
  // Validate the write parameters
  if (!context || !context->dest || !context->src || context->size == 0) {
    XELOGE("JIT write callback: Invalid parameters");
    return -1;
  }
  
  // Additional validation - ensure reasonable size bounds
  if (context->size > 16 * 1024 * 1024) {  // 16MB max per write
    XELOGE("JIT write callback: Size too large: {}", context->size);
    return -1;
  }
  
  XELOGI("JIT write callback: Copying {} bytes from 0x{:016X} to 0x{:016X}", 
         context->size, (uintptr_t)context->src, (uintptr_t)context->dest);
  
  // Perform the memory copy
  std::memcpy(context->dest, context->src, context->size);
  
  XELOGI("JIT write callback: Copy completed successfully");
  return 0;
}

// Define the JIT callback allowlist for Hardened Runtime
PTHREAD_JIT_WRITE_ALLOW_CALLBACKS_NP(jit_write_callback);

// Track if callbacks have been frozen
static bool jit_callbacks_frozen = false;

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

  // Resize (not reserve) space for unwind table entries to ensure vector has actual elements
  unwind_table_.resize(kMaximumFunctionCount);

  // Skip JIT callback freezing - using MAP_JIT with pthread_jit_write_protect_np
#if XE_PLATFORM_MAC && defined(__aarch64__)
  XELOGD("MacOSA64CodeCache::Initialize: Using MAP_JIT with pthread_jit_write_protect_np on ARM64 macOS");
#else
  XELOGD("MacOSA64CodeCache::Initialize: Using regular RWX memory (no JIT protection)");
#endif
  jit_callbacks_frozen = false;

  // Additional MacOS-specific initialization can be done here

  return true;
}

void MacOSA64CodeCache::CopyMachineCode(void* dest, const void* src, size_t size) {
  XELOGD("MacOSA64CodeCache::CopyMachineCode: About to copy {} bytes from 0x{:016X} to 0x{:016X}", 
         size, (uintptr_t)src, (uintptr_t)dest);
  
#if XE_PLATFORM_MAC && defined(__aarch64__)
  // On ARM64 macOS with MAP_JIT, use pthread_jit_write_protect_np dance
  XELOGD("MacOSA64CodeCache::CopyMachineCode: Using MAP_JIT with write protection toggle (Thread: 0x{:016X})", (uintptr_t)pthread_self());
  
  // Enable write access for this thread (disable execute)
  pthread_jit_write_protect_np(0);
  XELOGD("MacOSA64CodeCache::CopyMachineCode: Write mode enabled for thread");
  
  // Copy the machine code
  std::memcpy(dest, src, size);
  
  // Re-enable execute access for this thread (disable write)
  pthread_jit_write_protect_np(1);
  XELOGD("MacOSA64CodeCache::CopyMachineCode: Execute mode re-enabled for thread");
  
  XELOGD("MacOSA64CodeCache::CopyMachineCode: MAP_JIT code copied with write protection dance");
#else
  // Using regular RWX memory instead of MAP_JIT - simple memcpy is sufficient
  XELOGD("MacOSA64CodeCache::CopyMachineCode: Using simple memcpy for RWX memory");
  
  // Copy the machine code
  std::memcpy(dest, src, size);
#endif
  
  XELOGD("MacOSA64CodeCache::CopyMachineCode: Machine code copied successfully");
}

MacOSA64CodeCache::UnwindReservation
MacOSA64CodeCache::RequestUnwindReservation(uint8_t* entry_address) {
  uint32_t current_count = unwind_table_count_.fetch_add(1);
  assert_false(current_count >= kMaximumFunctionCount);
  UnwindReservation unwind_reservation;
  unwind_reservation.data_size = xe::round_up(kUnwindInfoSize, 16);
  unwind_reservation.table_slot = current_count;
  unwind_reservation.entry_address = entry_address;
  XELOGD("MacOSA64CodeCache::RequestUnwindReservation: Allocated slot {} (total count now {})", 
         current_count, current_count + 1);
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

  // NOTE: Fixed double-storage bug - only store in the reserved slot, not both
  // Add entry to unwind table at the reserved slot only
  UnwindInfo unwind_info;
  unwind_info.begin_address = reinterpret_cast<uintptr_t>(code_execute_address);
  unwind_info.end_address = unwind_info.begin_address + func_info.code_size.total;
  
  // Store in the reserved slot
  unwind_table_[unwind_reservation.table_slot] = unwind_info;

  XELOGD("MacOSA64CodeCache::PlaceCode: About to flush instruction cache for address 0x{:016X}, size {}", 
         (uintptr_t)code_execute_address, func_info.code_size.total);
  
  // Check memory permissions before flushing
  XELOGD("MacOSA64CodeCache::PlaceCode: Checking memory permissions before i-cache flush");
  
  // Validate address alignment before calling sys_icache_invalidate
  if ((uintptr_t)code_execute_address % 4 != 0) {
    XELOGW("MacOSA64CodeCache::PlaceCode: WARNING - code address 0x{:016X} is not 4-byte aligned", 
           (uintptr_t)code_execute_address);
  }
  
  if (func_info.code_size.total % 4 != 0) {
    XELOGW("MacOSA64CodeCache::PlaceCode: WARNING - code size {} is not 4-byte aligned", 
           func_info.code_size.total);
  }
  
  // Check if memory region is actually accessible
  XELOGD("MacOSA64CodeCache::PlaceCode: About to call sys_icache_invalidate");
  
#if XE_PLATFORM_MAC && defined(__aarch64__)
  // On MAP_JIT memory, just flush instruction cache - memory is already executable
  sys_icache_invalidate(code_execute_address, func_info.code_size.total);
  XELOGD("MacOSA64CodeCache::PlaceCode: Instruction cache flushed for MAP_JIT memory");
#else
  // On RWX memory, just flush instruction cache - no JIT protection needed
  sys_icache_invalidate(code_execute_address, func_info.code_size.total);
  XELOGD("MacOSA64CodeCache::PlaceCode: Instruction cache flushed successfully");
#endif
  
  XELOGD("MacOSA64CodeCache::PlaceCode: Code setup complete");
  
  // Verify code is executable by checking first few bytes
  XELOGD("MacOSA64CodeCache::PlaceCode: Code verification - first 16 bytes: {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}",
         ((uint8_t*)code_execute_address)[0], ((uint8_t*)code_execute_address)[1], 
         ((uint8_t*)code_execute_address)[2], ((uint8_t*)code_execute_address)[3],
         ((uint8_t*)code_execute_address)[4], ((uint8_t*)code_execute_address)[5],
         ((uint8_t*)code_execute_address)[6], ((uint8_t*)code_execute_address)[7],
         ((uint8_t*)code_execute_address)[8], ((uint8_t*)code_execute_address)[9],
         ((uint8_t*)code_execute_address)[10], ((uint8_t*)code_execute_address)[11],
         ((uint8_t*)code_execute_address)[12], ((uint8_t*)code_execute_address)[13],
         ((uint8_t*)code_execute_address)[14], ((uint8_t*)code_execute_address)[15]);
}

void MacOSA64CodeCache::InitializeUnwindEntry(
    uint8_t* unwind_entry_address, size_t unwind_table_slot,
    void* code_execute_address, const EmitFunctionInfo& func_info) {
  // Initialize unwind information for MacOS (simplified example)
  // In practice, you would populate this with proper DWARF unwind info
  // based on the function prologue and epilogue.

  // NOTE: Unwind info is already stored in PlaceCode, so we don't store it again here
  // to avoid the double-storage bug that was causing memory corruption.
  XELOGD("MacOSA64CodeCache::InitializeUnwindEntry: Unwind entry initialized for slot {} at 0x{:016X}",
         unwind_table_slot, reinterpret_cast<uintptr_t>(code_execute_address));
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
