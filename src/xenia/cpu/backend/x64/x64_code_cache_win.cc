/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/x64/x64_code_cache.h"

#include <cstdlib>
#include <cstring>

#include "xenia/base/assert.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/base/platform_win.h"
#include "xenia/cpu/function.h"

// When enabled, this will use Windows 8 APIs to get unwind info.
// TODO(benvanik): figure out why the callback variant doesn't work.
#define USE_GROWABLE_FUNCTION_TABLE

namespace xe {
namespace cpu {
namespace backend {
namespace x64 {

// Size of unwind info per function.
// TODO(benvanik): move this to emitter.
static const uint32_t kUnwindInfoSize = 4 + (2 * 1 + 2 + 2);

class Win32X64CodeCache : public X64CodeCache {
 public:
  Win32X64CodeCache();
  ~Win32X64CodeCache() override;

  bool Initialize() override;

  void* LookupUnwindInfo(uint64_t host_pc) override;

 private:
  UnwindReservation RequestUnwindReservation(uint8_t* entry_address) override;
  void PlaceCode(uint32_t guest_address, void* machine_code, size_t code_size,
                 size_t stack_size, void* code_address,
                 UnwindReservation unwind_reservation) override;

  void InitializeUnwindEntry(uint8_t* unwind_entry_address,
                             size_t unwind_table_slot, void* code_address,
                             size_t code_size, size_t stack_size);

  // Growable function table system handle.
  void* unwind_table_handle_ = nullptr;
  // Actual unwind table entries.
  std::vector<RUNTIME_FUNCTION> unwind_table_;
  // Current number of entries in the table.
  std::atomic<uint32_t> unwind_table_count_ = {0};
};

std::unique_ptr<X64CodeCache> X64CodeCache::Create() {
  return std::make_unique<Win32X64CodeCache>();
}

Win32X64CodeCache::Win32X64CodeCache() = default;

Win32X64CodeCache::~Win32X64CodeCache() {
#ifdef USE_GROWABLE_FUNCTION_TABLE
  if (unwind_table_handle_) {
    RtlDeleteGrowableFunctionTable(unwind_table_handle_);
  }
#else
  if (generated_code_base_) {
    RtlDeleteFunctionTable(reinterpret_cast<PRUNTIME_FUNCTION>(
        reinterpret_cast<DWORD64>(generated_code_base_) | 0x3));
  }
#endif  // USE_GROWABLE_FUNCTION_TABLE
}

bool Win32X64CodeCache::Initialize() {
  if (!X64CodeCache::Initialize()) {
    return false;
  }

  // Compute total number of unwind entries we should allocate.
  // We don't support reallocing right now, so this should be high.
  unwind_table_.resize(kMaximumFunctionCount);

#ifdef USE_GROWABLE_FUNCTION_TABLE
  // Create table and register with the system. It's empty now, but we'll grow
  // it as functions are added.
  if (RtlAddGrowableFunctionTable(
          &unwind_table_handle_, unwind_table_.data(), unwind_table_count_,
          DWORD(unwind_table_.size()),
          reinterpret_cast<ULONG_PTR>(generated_code_base_),
          reinterpret_cast<ULONG_PTR>(generated_code_base_ +
                                      kGeneratedCodeSize))) {
    XELOGE("Unable to create unwind function table");
    return false;
  }
#else
  // Install a callback that the debugger will use to lookup unwind info on
  // demand.
  if (!RtlInstallFunctionTableCallback(
          reinterpret_cast<DWORD64>(generated_code_base_) | 0x3,
          reinterpret_cast<DWORD64>(generated_code_base_), kGeneratedCodeSize,
          [](uintptr_t control_pc, void* context) {
            auto code_cache = reinterpret_cast<X64CodeCache*>(context);
            return reinterpret_cast<PRUNTIME_FUNCTION>(
                code_cache->LookupUnwindEntry(control_pc));
          },
          this, nullptr)) {
    XELOGE("Unable to install function table callback");
    return false;
  }
#endif  // USE_GROWABLE_FUNCTION_TABLE

  return true;
}

Win32X64CodeCache::UnwindReservation
Win32X64CodeCache::RequestUnwindReservation(uint8_t* entry_address) {
  UnwindReservation unwind_reservation;
  unwind_reservation.data_size = xe::round_up(kUnwindInfoSize, 16);
  unwind_reservation.table_slot = ++unwind_table_count_;
  unwind_reservation.entry_address = entry_address;
  assert_false(unwind_table_count_ >= kMaximumFunctionCount);

  return unwind_reservation;
}

void Win32X64CodeCache::PlaceCode(uint32_t guest_address, void* machine_code,
                                  size_t code_size, size_t stack_size,
                                  void* code_address,
                                  UnwindReservation unwind_reservation) {
  // Add unwind info.
  InitializeUnwindEntry(unwind_reservation.entry_address,
                        unwind_reservation.table_slot, code_address, code_size,
                        stack_size);

#ifdef USE_GROWABLE_FUNCTION_TABLE
  // Notify that the unwind table has grown.
  // We do this outside of the lock, but with the latest total count.
  RtlGrowFunctionTable(unwind_table_handle_, unwind_table_count_);
#endif  // USE_GROWABLE_FUNCTION_TABLE

  // This isn't needed on x64 (probably), but is convention.
  FlushInstructionCache(GetCurrentProcess(), code_address, code_size);
}

// http://msdn.microsoft.com/en-us/library/ssa62fwe.aspx
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

void Win32X64CodeCache::InitializeUnwindEntry(uint8_t* unwind_entry_address,
                                              size_t unwind_table_slot,
                                              void* code_address,
                                              size_t code_size,
                                              size_t stack_size) {
  auto unwind_info = reinterpret_cast<UNWIND_INFO*>(unwind_entry_address);

  if (!stack_size) {
    // http://msdn.microsoft.com/en-us/library/ddssxxy8.aspx
    unwind_info->Version = 1;
    unwind_info->Flags = 0;
    unwind_info->SizeOfProlog = 0;
    unwind_info->CountOfCodes = 0;
    unwind_info->FrameRegister = 0;
    unwind_info->FrameOffset = 0;
  } else if (stack_size <= 128) {
    uint8_t prolog_size = 4;

    // http://msdn.microsoft.com/en-us/library/ddssxxy8.aspx
    unwind_info->Version = 1;
    unwind_info->Flags = 0;
    unwind_info->SizeOfProlog = prolog_size;
    unwind_info->CountOfCodes = 1;
    unwind_info->FrameRegister = 0;
    unwind_info->FrameOffset = 0;

    // http://msdn.microsoft.com/en-us/library/ck9asaa9.aspx
    size_t co = 0;
    auto& unwind_code = unwind_info->UnwindCode[co++];
    unwind_code.CodeOffset =
        14;  // end of instruction + 1 == offset of next instruction
    unwind_code.UnwindOp = UWOP_ALLOC_SMALL;
    unwind_code.OpInfo = stack_size / 8 - 1;
  } else {
    // TODO(benvanik): take as parameters?
    uint8_t prolog_size = 7;

    // http://msdn.microsoft.com/en-us/library/ddssxxy8.aspx
    unwind_info->Version = 1;
    unwind_info->Flags = 0;
    unwind_info->SizeOfProlog = prolog_size;
    unwind_info->CountOfCodes = 2;
    unwind_info->FrameRegister = 0;
    unwind_info->FrameOffset = 0;

    // http://msdn.microsoft.com/en-us/library/ck9asaa9.aspx
    size_t co = 0;
    auto& unwind_code = unwind_info->UnwindCode[co++];
    unwind_code.CodeOffset =
        7;  // end of instruction + 1 == offset of next instruction
    unwind_code.UnwindOp = UWOP_ALLOC_LARGE;
    unwind_code.OpInfo = 0;

    assert_true((stack_size / 8) < 65536u);
    unwind_code = unwind_info->UnwindCode[co++];
    unwind_code.FrameOffset = (USHORT)(stack_size) / 8;
  }

  // Add entry.
  auto& fn_entry = unwind_table_[unwind_table_slot];
  fn_entry.BeginAddress =
      (DWORD)(reinterpret_cast<uint8_t*>(code_address) - generated_code_base_);
  fn_entry.EndAddress = (DWORD)(fn_entry.BeginAddress + code_size);
  fn_entry.UnwindData = (DWORD)(unwind_entry_address - generated_code_base_);
}

void* Win32X64CodeCache::LookupUnwindInfo(uint64_t host_pc) {
  return std::bsearch(
      &host_pc, unwind_table_.data(), unwind_table_count_ + 1,
      sizeof(RUNTIME_FUNCTION),
      [](const void* key_ptr, const void* element_ptr) {
        auto key =
            *reinterpret_cast<const uintptr_t*>(key_ptr) - kGeneratedCodeBase;
        auto element = reinterpret_cast<const RUNTIME_FUNCTION*>(element_ptr);
        if (key < element->BeginAddress) {
          return -1;
        } else if (key > element->EndAddress) {
          return 1;
        } else {
          return 0;
        }
      });
}

}  // namespace x64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
