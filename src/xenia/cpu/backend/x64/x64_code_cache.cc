/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/x64/x64_code_cache.h"

#include <cstring>

#include "xenia/base/assert.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"

namespace xe {
namespace cpu {
namespace backend {
namespace x64 {

// Size of unwind info per function.
// TODO(benvanik): move this to emitter.
const static uint32_t kUnwindInfoSize = 4 + (2 * 1 + 2 + 2);

X64CodeCache::X64CodeCache()
    : mapping_(nullptr),
      indirection_default_value_(0xFEEDF00D),
      indirection_table_base_(nullptr),
      generated_code_base_(nullptr),
      generated_code_offset_(0),
      generated_code_commit_mark_(0),
      unwind_table_handle_(nullptr),
      unwind_table_count_(0) {}

X64CodeCache::~X64CodeCache() {
  if (unwind_table_handle_) {
    RtlDeleteGrowableFunctionTable(unwind_table_handle_);
  }
  if (indirection_table_base_) {
    VirtualFree(indirection_table_base_, 0, MEM_RELEASE);
  }
  // Unmap all views and close mapping.
  if (mapping_) {
    UnmapViewOfFile(generated_code_base_);
    CloseHandle(mapping_);
    mapping_ = 0;
  }
}

bool X64CodeCache::Initialize() {
  indirection_table_base_ = reinterpret_cast<uint8_t*>(
      VirtualAlloc(reinterpret_cast<void*>(kIndirectionTableBase),
                   kIndirectionTableSize, MEM_RESERVE, PAGE_READWRITE));
  if (!indirection_table_base_) {
    XELOGE("Unable to allocate code cache indirection table");
    XELOGE(
        "This is likely because the %.8X-%.8X range is in use by some other "
        "system DLL",
        kIndirectionTableBase, kIndirectionTableBase + kIndirectionTableSize);
    return false;
  }

  // Create mmap file. This allows us to share the code cache with the debugger.
  wchar_t file_name[256];
  wsprintf(file_name, L"Local\\xenia_code_cache_%p", Clock::QueryHostTickCount());
  file_name_ = file_name;
  mapping_ = CreateFileMapping(INVALID_HANDLE_VALUE, NULL,
                               PAGE_EXECUTE_READWRITE | SEC_RESERVE, 0,
                               kGeneratedCodeSize, file_name_.c_str());
  if (!mapping_) {
    XELOGE("Unable to create code cache mmap");
    return false;
  }

  // Mapp generated code region into the file. Pages are committed as required.
  generated_code_base_ = reinterpret_cast<uint8_t*>(MapViewOfFileEx(
      mapping_, FILE_MAP_ALL_ACCESS | FILE_MAP_EXECUTE, 0, 0,
      kGeneratedCodeSize, reinterpret_cast<void*>(kGeneratedCodeBase)));
  if (!generated_code_base_) {
    XELOGE("Unable to allocate code cache generated code storage");
    XELOGE(
        "This is likely because the %.8X-%.8X range is in use by some other "
        "system DLL",
        kGeneratedCodeBase, kGeneratedCodeBase + kGeneratedCodeSize);
    return false;
  }

  // Compute total number of unwind entries we should allocate.
  // We don't support reallocing right now, so this should be high.
  unwind_table_.resize(30000);

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

  return true;
}

void X64CodeCache::set_indirection_default(uint32_t default_value) {
  indirection_default_value_ = default_value;
}

void X64CodeCache::AddIndirection(uint32_t guest_address,
                                  uint32_t host_address) {
  uint32_t* indirection_slot = reinterpret_cast<uint32_t*>(
      indirection_table_base_ + (guest_address - kIndirectionTableBase));
  *indirection_slot = host_address;
}

void X64CodeCache::CommitExecutableRange(uint32_t guest_low,
                                         uint32_t guest_high) {
  // Commit the memory.
  VirtualAlloc(indirection_table_base_ + (guest_low - kIndirectionTableBase),
               guest_high - guest_low, MEM_COMMIT, PAGE_EXECUTE_READWRITE);

  // Fill memory with the default value.
  uint32_t* p = reinterpret_cast<uint32_t*>(indirection_table_base_);
  for (uint32_t address = guest_low; address < guest_high; ++address) {
    p[(address - kIndirectionTableBase) / 4] = indirection_default_value_;
  }
}

void* X64CodeCache::PlaceCode(uint32_t guest_address, void* machine_code,
                              size_t code_size, size_t stack_size) {
  // Hold a lock while we bump the pointers up. This is important as the
  // unwind table requires entries AND code to be sorted in order.
  size_t low_mark;
  size_t high_mark;
  uint8_t* code_address = nullptr;
  uint8_t* unwind_entry_address = nullptr;
  size_t unwind_table_slot = 0;
  {
    std::lock_guard<xe::mutex> allocation_lock(allocation_mutex_);

    low_mark = generated_code_offset_;

    // Reserve code.
    // Always move the code to land on 16b alignment.
    code_address = generated_code_base_ + generated_code_offset_;
    generated_code_offset_ += xe::round_up(code_size, 16);

    // Reserve unwind info.
    // We go on the high size of the unwind info as we don't know how big we
    // need it, and a few extra bytes of padding isn't the worst thing.
    unwind_entry_address = generated_code_base_ + generated_code_offset_;
    generated_code_offset_ += xe::round_up(kUnwindInfoSize, 16);
    unwind_table_slot = ++unwind_table_count_;

    high_mark = generated_code_offset_;
  }

  // If we are going above the high water mark of committed memory, commit some
  // more. It's ok if multiple threads do this, as redundant commits aren't
  // harmful.
  size_t old_commit_mark = generated_code_commit_mark_;
  if (high_mark > old_commit_mark) {
    size_t new_commit_mark = old_commit_mark + 16 * 1024 * 1024;
    VirtualAlloc(generated_code_base_, new_commit_mark, MEM_COMMIT,
                 PAGE_EXECUTE_READWRITE);
    generated_code_commit_mark_.compare_exchange_strong(old_commit_mark,
                                                        new_commit_mark);
  }

  // Copy code.
  std::memcpy(code_address, machine_code, code_size);

  // Add unwind info.
  InitializeUnwindEntry(unwind_entry_address, unwind_table_slot, code_address,
                        code_size, stack_size);

  // Notify that the unwind table has grown.
  // We do this outside of the lock, but with the latest total count.
  RtlGrowFunctionTable(unwind_table_handle_, unwind_table_count_);

  // This isn't needed on x64 (probably), but is convention.
  FlushInstructionCache(GetCurrentProcess(), code_address, code_size);

  // Now that everything is ready, fix up the indirection table.
  // Note that we do support code that doesn't have an indirection fixup, so
  // ignore those when we see them.
  if (guest_address) {
    uint32_t* indirection_slot = reinterpret_cast<uint32_t*>(
        indirection_table_base_ + (guest_address - kIndirectionTableBase));
    *indirection_slot = uint32_t(reinterpret_cast<uint64_t>(code_address));
  }

  return code_address;
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

void X64CodeCache::InitializeUnwindEntry(uint8_t* unwind_entry_address,
                                         size_t unwind_table_slot,
                                         uint8_t* code_address,
                                         size_t code_size, size_t stack_size) {
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
    unwind_code = unwind_info->UnwindCode[co++];
    unwind_code.FrameOffset = (USHORT)(stack_size) / 8;
  }

  // Add entry.
  auto& fn_entry = unwind_table_[unwind_table_slot];
  fn_entry.BeginAddress = (DWORD)(code_address - generated_code_base_);
  fn_entry.EndAddress = (DWORD)(fn_entry.BeginAddress + code_size);
  fn_entry.UnwindData = (DWORD)(unwind_entry_address - generated_code_base_);
}

uint32_t X64CodeCache::PlaceData(const void* data, size_t length) {
  // Hold a lock while we bump the pointers up.
  size_t high_mark;
  uint8_t* data_address = nullptr;
  size_t unwind_table_slot = 0;
  {
    std::lock_guard<xe::mutex> allocation_lock(allocation_mutex_);

    // Reserve code.
    // Always move the code to land on 16b alignment.
    data_address = generated_code_base_ + generated_code_offset_;
    generated_code_offset_ += xe::round_up(length, 16);

    high_mark = generated_code_offset_;
  }

  // If we are going above the high water mark of committed memory, commit some
  // more. It's ok if multiple threads do this, as redundant commits aren't
  // harmful.
  size_t old_commit_mark = generated_code_commit_mark_;
  if (high_mark > old_commit_mark) {
    size_t new_commit_mark = old_commit_mark + 16 * 1024 * 1024;
    VirtualAlloc(generated_code_base_, new_commit_mark, MEM_COMMIT,
                 PAGE_EXECUTE_READWRITE);
    generated_code_commit_mark_.compare_exchange_strong(old_commit_mark,
                                                        new_commit_mark);
  }

  // Copy code.
  std::memcpy(data_address, data, length);

  return uint32_t(uintptr_t(data_address));
}

}  // namespace x64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
