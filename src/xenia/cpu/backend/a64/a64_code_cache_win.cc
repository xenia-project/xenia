/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/a64/a64_code_cache.h"

#include <cstdlib>
#include <cstring>

#include "xenia/base/assert.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/base/platform_win.h"
#include "xenia/cpu/function.h"

// Function pointer definitions
using FnRtlAddGrowableFunctionTable = decltype(&RtlAddGrowableFunctionTable);
using FnRtlGrowFunctionTable = decltype(&RtlGrowFunctionTable);
using FnRtlDeleteGrowableFunctionTable =
    decltype(&RtlDeleteGrowableFunctionTable);

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

// ARM64 unwind-op codes
// https://docs.microsoft.com/en-us/cpp/build/arm64-exception-handling#unwind-codes
// https://www.corsix.org/content/windows-arm64-unwind-codes
typedef enum _UNWIND_OP_CODES {
  UWOP_NOP = 0xE3,
  UWOP_ALLOC_S = 0x00,           // sub sp, sp, i*16
  UWOP_ALLOC_L = 0xE0'00'00'00,  // sub sp, sp, i*16
  UWOP_SAVE_FPLR = 0x40,         // stp fp, lr, [sp+i*8]
  UWOP_SAVE_FPLRX = 0x80,        // stp fp, lr, [sp-(i+1)*8]!
  UWOP_SET_FP = 0xE1,            // mov fp, sp
  UWOP_END = 0xE4,
} UNWIND_CODE_OPS;

using UNWIND_CODE = uint32_t;

static_assert(sizeof(UNWIND_CODE) == sizeof(uint32_t));

// UNWIND_INFO defines the static part (first 32-bit) of the .xdata record
typedef struct _UNWIND_INFO {
  uint32_t FunctionLength : 18;
  uint32_t Version : 2;
  uint32_t X : 1;
  uint32_t E : 1;
  uint32_t EpilogCount : 5;
  uint32_t CodeWords : 5;
  UNWIND_CODE UnwindCodes[2];
} UNWIND_INFO, *PUNWIND_INFO;

static_assert(offsetof(UNWIND_INFO, UnwindCodes[0]) == 4);
static_assert(offsetof(UNWIND_INFO, UnwindCodes[1]) == 8);

// Size of unwind info per function.
static const uint32_t kUnwindInfoSize = sizeof(UNWIND_INFO);

class Win32A64CodeCache : public A64CodeCache {
 public:
  Win32A64CodeCache();
  ~Win32A64CodeCache() override;

  bool Initialize() override;

  void* LookupUnwindInfo(uint64_t host_pc) override;

 private:
  UnwindReservation RequestUnwindReservation(uint8_t* entry_address) override;
  void PlaceCode(uint32_t guest_address, void* machine_code,
                 const EmitFunctionInfo& func_info, void* code_execute_address,
                 UnwindReservation unwind_reservation) override;

  void InitializeUnwindEntry(uint8_t* unwind_entry_address,
                             size_t unwind_table_slot,
                             void* code_execute_address,
                             const EmitFunctionInfo& func_info);

  // Growable function table system handle.
  void* unwind_table_handle_ = nullptr;
  // Actual unwind table entries.
  std::vector<RUNTIME_FUNCTION> unwind_table_;
  // Current number of entries in the table.
  std::atomic<uint32_t> unwind_table_count_ = {0};
  // Does this version of Windows support growable funciton tables?
  bool supports_growable_table_ = false;

  FnRtlAddGrowableFunctionTable add_growable_table_ = nullptr;
  FnRtlDeleteGrowableFunctionTable delete_growable_table_ = nullptr;
  FnRtlGrowFunctionTable grow_table_ = nullptr;
};

std::unique_ptr<A64CodeCache> A64CodeCache::Create() {
  return std::make_unique<Win32A64CodeCache>();
}

Win32A64CodeCache::Win32A64CodeCache() = default;

Win32A64CodeCache::~Win32A64CodeCache() {
  if (supports_growable_table_) {
    if (unwind_table_handle_) {
      delete_growable_table_(unwind_table_handle_);
    }
  } else {
    if (generated_code_execute_base_) {
      RtlDeleteFunctionTable(reinterpret_cast<PRUNTIME_FUNCTION>(
          reinterpret_cast<DWORD64>(generated_code_execute_base_) | 0x3));
    }
  }
}

bool Win32A64CodeCache::Initialize() {
  if (!A64CodeCache::Initialize()) {
    return false;
  }

  // Compute total number of unwind entries we should allocate.
  // We don't support reallocing right now, so this should be high.
  unwind_table_.resize(kMaximumFunctionCount);

  // Check if this version of Windows supports growable function tables.
  auto ntdll_handle = GetModuleHandleW(L"ntdll.dll");
  if (!ntdll_handle) {
    add_growable_table_ = nullptr;
    delete_growable_table_ = nullptr;
    grow_table_ = nullptr;
  } else {
    add_growable_table_ = (FnRtlAddGrowableFunctionTable)GetProcAddress(
        ntdll_handle, "RtlAddGrowableFunctionTable");
    delete_growable_table_ = (FnRtlDeleteGrowableFunctionTable)GetProcAddress(
        ntdll_handle, "RtlDeleteGrowableFunctionTable");
    grow_table_ = (FnRtlGrowFunctionTable)GetProcAddress(
        ntdll_handle, "RtlGrowFunctionTable");
  }
  supports_growable_table_ =
      add_growable_table_ && delete_growable_table_ && grow_table_;

  // Create table and register with the system. It's empty now, but we'll grow
  // it as functions are added.
  if (supports_growable_table_) {
    if (add_growable_table_(
            &unwind_table_handle_, unwind_table_.data(), unwind_table_count_,
            DWORD(unwind_table_.size()),
            reinterpret_cast<ULONG_PTR>(generated_code_execute_base_),
            reinterpret_cast<ULONG_PTR>(generated_code_execute_base_ +
                                        kGeneratedCodeSize))) {
      XELOGE("Unable to create unwind function table");
      return false;
    }
  } else {
    // Install a callback that the debugger will use to lookup unwind info on
    // demand.
    if (!RtlInstallFunctionTableCallback(
            reinterpret_cast<DWORD64>(generated_code_execute_base_) | 0x3,
            reinterpret_cast<DWORD64>(generated_code_execute_base_),
            kGeneratedCodeSize,
            [](DWORD64 control_pc, PVOID context) {
              auto code_cache = reinterpret_cast<Win32A64CodeCache*>(context);
              return reinterpret_cast<PRUNTIME_FUNCTION>(
                  code_cache->LookupUnwindInfo(control_pc));
            },
            this, nullptr)) {
      XELOGE("Unable to install function table callback");
      return false;
    }
  }

  return true;
}

Win32A64CodeCache::UnwindReservation
Win32A64CodeCache::RequestUnwindReservation(uint8_t* entry_address) {
  assert_false(unwind_table_count_ >= kMaximumFunctionCount);
  UnwindReservation unwind_reservation;
  unwind_reservation.data_size = xe::round_up(kUnwindInfoSize, 16);
  unwind_reservation.table_slot = unwind_table_count_++;
  unwind_reservation.entry_address = entry_address;
  return unwind_reservation;
}

void Win32A64CodeCache::PlaceCode(uint32_t guest_address, void* machine_code,
                                  const EmitFunctionInfo& func_info,
                                  void* code_execute_address,
                                  UnwindReservation unwind_reservation) {
  // Add unwind info.
  InitializeUnwindEntry(unwind_reservation.entry_address,
                        unwind_reservation.table_slot, code_execute_address,
                        func_info);

  if (supports_growable_table_) {
    // Notify that the unwind table has grown.
    // We do this outside of the lock, but with the latest total count.
    grow_table_(unwind_table_handle_, unwind_table_count_);
  }

  // https://docs.microsoft.com/en-us/uwp/win32-and-com/win32-apis
  FlushInstructionCache(GetCurrentProcess(), code_execute_address,
                        func_info.code_size.total);
}

constexpr UNWIND_CODE UnwindOpWord(uint8_t code0 = UWOP_NOP,
                                   uint8_t code1 = UWOP_NOP,
                                   uint8_t code2 = UWOP_NOP,
                                   uint8_t code3 = UWOP_NOP) {
  return static_cast<uint32_t>(code0) | (static_cast<uint32_t>(code1) << 8) |
         (static_cast<uint32_t>(code2) << 16) |
         (static_cast<uint32_t>(code3) << 24);
}

// 8-byte unwind code for "stp fp, lr, [sp, #-16]!
// https://docs.microsoft.com/en-us/cpp/build/arm64-exception-handling#unwind-codes
static uint8_t OpSaveFpLrX(int16_t pre_index_offset) {
  assert_true(pre_index_offset <= -8);
  assert_true(pre_index_offset >= -512);
  // 16-byte aligned
  constexpr int IndexShift = 3;
  constexpr int IndexMask = (1 << IndexShift) - 1;
  assert_true((pre_index_offset & IndexMask) == 0);
  const uint32_t encoded_value = (-pre_index_offset >> IndexShift) - 1;
  return UWOP_SAVE_FPLRX | encoded_value;
}

// Ensure a 16-byte aligned stack
static constexpr size_t StackAlignShift = 4;                          // n / 16
static constexpr size_t StackAlignMask = (1 << StackAlignShift) - 1;  // n % 16

// 8-byte unwind code for up to +512-byte "sub sp, sp, #stack_space"
// https://docs.microsoft.com/en-us/cpp/build/arm64-exception-handling#unwind-codes
static uint8_t OpAllocS(int16_t stack_space) {
  assert_true(stack_space >= 0);
  assert_true(stack_space < 512);
  assert_true((stack_space & StackAlignMask) == 0);
  return UWOP_ALLOC_S | (stack_space >> StackAlignShift);
}

// 4-byte unwind code for +256MiB "sub sp, sp, #stack_space"
// https://docs.microsoft.com/en-us/cpp/build/arm64-exception-handling#unwind-codes
uint32_t OpAllocL(int32_t stack_space) {
  assert_true(stack_space >= 0);
  assert_true(stack_space < (0xFFFFFF * 16));
  assert_true((stack_space & StackAlignMask) == 0);
  return xe::byte_swap(UWOP_ALLOC_L |
                       ((stack_space >> StackAlignShift) & 0xFF'FF'FF));
}

void Win32A64CodeCache::InitializeUnwindEntry(
    uint8_t* unwind_entry_address, size_t unwind_table_slot,
    void* code_execute_address, const EmitFunctionInfo& func_info) {
  auto unwind_info = reinterpret_cast<UNWIND_INFO*>(unwind_entry_address);

  *unwind_info = {};
  // ARM64 instructions are always multiples of 4 bytes
  // Windows ignores the bottom 2 bits
  unwind_info->FunctionLength = func_info.code_size.total / 4;
  unwind_info->CodeWords = 2;

  // https://learn.microsoft.com/en-us/cpp/build/arm64-exception-handling?view=msvc-170#unwind-codes
  // The array of unwind codes is a pool of sequences that describe exactly how
  // to undo the effects of the prolog. They're stored in the same order the
  // operations need to be undone. The unwind codes can be thought of as a small
  // instruction set, encoded as a string of bytes. When execution is complete,
  // the return address to the calling function is in the lr register. And, all
  // non-volatile registers are restored to their values at the time the
  // function was called.

  // Function frames are generally:
  // STP(X29, X30, SP, PRE_INDEXED, -32);
  // MOV(X29, XSP);
  // SUB(XSP, XSP, stack_size);
  // ... function body ...
  // ADD(XSP, XSP, stack_size);
  // MOV(XSP, X29);
  // LDP(X29, X30, SP, POST_INDEXED, 32);

  // These opcodes must undo the epilog and put the return address within lr
  unwind_info->UnwindCodes[0] = OpAllocL(func_info.stack_size);
  unwind_info->UnwindCodes[1] =
      UnwindOpWord(UWOP_SET_FP, OpSaveFpLrX(-32), UWOP_END);

  // Add entry.
  RUNTIME_FUNCTION& fn_entry = unwind_table_[unwind_table_slot];
  fn_entry.BeginAddress =
      DWORD(reinterpret_cast<uint8_t*>(code_execute_address) -
            generated_code_execute_base_);
  fn_entry.UnwindData =
      DWORD(unwind_entry_address - generated_code_execute_base_);
}

void* Win32A64CodeCache::LookupUnwindInfo(uint64_t host_pc) {
  return std::bsearch(
      &host_pc, unwind_table_.data(), unwind_table_count_,
      sizeof(RUNTIME_FUNCTION),
      [](const void* key_ptr, const void* element_ptr) {
        auto key = *reinterpret_cast<const uintptr_t*>(key_ptr) -
                   kGeneratedCodeExecuteBase;
        auto element = reinterpret_cast<const RUNTIME_FUNCTION*>(element_ptr);
        if (key < element->BeginAddress) {
          return -1;
        } else if (key > (element->BeginAddress + element->FunctionLength)) {
          return 1;
        } else {
          return 0;
        }
      });
}

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
