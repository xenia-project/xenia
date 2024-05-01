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

// https://msdn.microsoft.com/en-us/library/ssa62fwe.aspx
typedef enum _UNWIND_OP_CODES {
  UWOP_PUSH_NONVOL = 0, /* info == register number */
  UWOP_ALLOC_LARGE,     /* no info, alloc size in next 2 slots */
  UWOP_ALLOC_SMALL,     /* info == size of allocation / 8 - 1 */
  UWOP_SET_FPREG,       /* no info, FP = RSP + UNWIND_INFO.FPRegOffset*16 */
  UWOP_SAVE_NONVOL,     /* info == register number, offset in next slot */
  UWOP_SAVE_NONVOL_FAR, /* info == register number, offset in next 2 slots */
  UWOP_SAVE_XMM128,     /* info == XMM reg number, offset in next slot */
  UWOP_SAVE_XMM128_FAR, /* info == XMM reg number, offset in next 2 slots */
  UWOP_PUSH_MACHFRAME   /* info == 0: no error-code, 1: error-code */
} UNWIND_CODE_OPS;
class UNWIND_REGISTER {
 public:
  enum _ {
    RAX = 0,
    RCX = 1,
    RDX = 2,
    RBX = 3,
    RSP = 4,
    RBP = 5,
    RSI = 6,
    RDI = 7,
    R8 = 8,
    R9 = 9,
    R10 = 10,
    R11 = 11,
    R12 = 12,
    R13 = 13,
    R14 = 14,
    R15 = 15,
  };
};

typedef union _UNWIND_CODE {
  struct {
    uint8_t CodeOffset;
    uint8_t UnwindOp : 4;
    uint8_t OpInfo : 4;
  };
  USHORT FrameOffset;
} UNWIND_CODE, *PUNWIND_CODE;

typedef struct _UNWIND_INFO {
  uint8_t Version : 3;
  uint8_t Flags : 5;
  uint8_t SizeOfProlog;
  uint8_t CountOfCodes;
  uint8_t FrameRegister : 4;
  uint8_t FrameOffset : 4;
  UNWIND_CODE UnwindCode[1];
  /*  UNWIND_CODE MoreUnwindCode[((CountOfCodes + 1) & ~1) - 1];
   *   union {
   *       OPTIONAL ULONG ExceptionHandler;
   *       OPTIONAL ULONG FunctionEntry;
   *   };
   *   OPTIONAL ULONG ExceptionData[]; */
} UNWIND_INFO, *PUNWIND_INFO;

// Size of unwind info per function.
// TODO(benvanik): move this to emitter.
static const uint32_t kUnwindInfoSize =
    sizeof(UNWIND_INFO) + (sizeof(UNWIND_CODE) * (6 - 1));

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

  // This isn't needed on a64 (probably), but is convention.
  // On UWP, FlushInstructionCache available starting from 10.0.16299.0.
  // https://docs.microsoft.com/en-us/uwp/win32-and-com/win32-apis
  FlushInstructionCache(GetCurrentProcess(), code_execute_address,
                        func_info.code_size.total);
}

void Win32A64CodeCache::InitializeUnwindEntry(
    uint8_t* unwind_entry_address, size_t unwind_table_slot,
    void* code_execute_address, const EmitFunctionInfo& func_info) {
  auto unwind_info = reinterpret_cast<UNWIND_INFO*>(unwind_entry_address);
  UNWIND_CODE* unwind_code = nullptr;

  assert_true(func_info.code_size.prolog < 256);  // needs to fit into a uint8_t
  auto prolog_size = static_cast<uint8_t>(func_info.code_size.prolog);
  assert_true(func_info.prolog_stack_alloc_offset <
              256);  // needs to fit into a uint8_t
  auto prolog_stack_alloc_offset =
      static_cast<uint8_t>(func_info.prolog_stack_alloc_offset);

  if (!func_info.stack_size) {
    // https://docs.microsoft.com/en-us/cpp/build/exception-handling-x64#struct-unwind_info
    unwind_info->Version = 1;
    unwind_info->Flags = 0;
    unwind_info->SizeOfProlog = prolog_size;
    unwind_info->CountOfCodes = 0;
    unwind_info->FrameRegister = 0;
    unwind_info->FrameOffset = 0;
  } else if (func_info.stack_size <= 128) {
    // https://docs.microsoft.com/en-us/cpp/build/exception-handling-x64#struct-unwind_info
    unwind_info->Version = 1;
    unwind_info->Flags = 0;
    unwind_info->SizeOfProlog = prolog_size;
    unwind_info->CountOfCodes = 0;
    unwind_info->FrameRegister = 0;
    unwind_info->FrameOffset = 0;

    // https://docs.microsoft.com/en-us/cpp/build/exception-handling-x64#struct-unwind_code
    unwind_code = &unwind_info->UnwindCode[unwind_info->CountOfCodes++];
    unwind_code->CodeOffset = prolog_stack_alloc_offset;
    unwind_code->UnwindOp = UWOP_ALLOC_SMALL;
    unwind_code->OpInfo = (func_info.stack_size / 8) - 1;
  } else {
    // TODO(benvanik): take as parameters?

    // https://docs.microsoft.com/en-us/cpp/build/exception-handling-x64#struct-unwind_info
    unwind_info->Version = 1;
    unwind_info->Flags = 0;
    unwind_info->SizeOfProlog = prolog_size;
    unwind_info->CountOfCodes = 0;
    unwind_info->FrameRegister = 0;
    unwind_info->FrameOffset = 0;

    // https://docs.microsoft.com/en-us/cpp/build/exception-handling-x64#struct-unwind_code
    unwind_code = &unwind_info->UnwindCode[unwind_info->CountOfCodes++];
    unwind_code->CodeOffset = prolog_stack_alloc_offset;
    unwind_code->UnwindOp = UWOP_ALLOC_LARGE;
    unwind_code->OpInfo = 0;  // One slot for size

    assert_true((func_info.stack_size / 8) < 65536u);
    unwind_code = &unwind_info->UnwindCode[unwind_info->CountOfCodes++];
    unwind_code->FrameOffset = (USHORT)(func_info.stack_size) / 8;
  }

  if (unwind_info->CountOfCodes % 1) {
    // Count of unwind codes must always be even.
    std::memset(&unwind_info->UnwindCode[unwind_info->CountOfCodes + 1], 0,
                sizeof(UNWIND_CODE));
  }

  // Add entry.
  auto& fn_entry = unwind_table_[unwind_table_slot];
  fn_entry.BeginAddress =
      DWORD(reinterpret_cast<uint8_t*>(code_execute_address) -
            generated_code_execute_base_);
  fn_entry.FunctionLength =
      DWORD(func_info.code_size.total);
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
