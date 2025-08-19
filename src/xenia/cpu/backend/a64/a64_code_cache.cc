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

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/base/assert.h"
#include "xenia/base/clock.h"
#include "xenia/base/literals.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/cpu/function.h"
#include "xenia/cpu/module.h"

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

using namespace xe::literals;

// Define static constants for linking
const size_t A64CodeCache::kIndirectionTableSize;
#if XE_PLATFORM_MAC && XE_ARCH_ARM64
// On macOS ARM64, this will be set dynamically during initialization
uintptr_t A64CodeCache::kIndirectionTableBase = 0x80000000;
#else
const uintptr_t A64CodeCache::kIndirectionTableBase;
#endif

A64CodeCache::A64CodeCache() = default;

A64CodeCache::~A64CodeCache() {
  if (indirection_table_base_) {
    xe::memory::DeallocFixed(indirection_table_base_, 0,
                             xe::memory::DeallocationType::kRelease);
  }

  // Unmap all views and close mapping.
  if (mapping_ != xe::memory::kFileMappingHandleInvalid) {
#if XE_PLATFORM_MAC && XE_ARCH_ARM64
    // On macOS ARM64, we used AllocFixed instead of MapFileView, so use DeallocFixed
    if (generated_code_execute_base_) {
      xe::memory::DeallocFixed(generated_code_execute_base_, kGeneratedCodeSize,
                               xe::memory::DeallocationType::kRelease);
    }
#else
    // Other platforms use MapFileView/UnmapFileView
    if (generated_code_write_base_ &&
        generated_code_write_base_ != generated_code_execute_base_) {
      xe::memory::UnmapFileView(mapping_, generated_code_write_base_,
                                kGeneratedCodeSize);
    }
    if (generated_code_execute_base_) {
      xe::memory::UnmapFileView(mapping_, generated_code_execute_base_,
                                kGeneratedCodeSize);
    }
#endif
    xe::memory::CloseFileMappingHandle(mapping_, file_name_);
    mapping_ = xe::memory::kFileMappingHandleInvalid;
  }
}

bool A64CodeCache::Initialize() {
#if XE_PLATFORM_MAC && XE_ARCH_ARM64
  // On macOS ARM64, allocate the indirection table wherever the OS allows,
  // then update our base address to match. This gives us the same direct
  // access pattern as x64 without needing complex offset calculations.
  
  indirection_table_base_ = reinterpret_cast<uint8_t*>(xe::memory::AllocFixed(
      nullptr, kIndirectionTableSize,
      xe::memory::AllocationType::kReserveCommit,
      xe::memory::PageAccess::kReadWrite));
      
  if (!indirection_table_base_) {
    XELOGE("Unable to allocate indirection table at any address");
    return false;
  }
  
  // Keep kIndirectionTableBase as 0x80000000 for offset calculations
  // Store the actual allocated address separately
  indirection_table_actual_base_ = reinterpret_cast<uintptr_t>(indirection_table_base_);
#else
  // Other platforms: try to allocate at the preferred address
  indirection_table_base_ = reinterpret_cast<uint8_t*>(xe::memory::AllocFixed(
      reinterpret_cast<void*>(kIndirectionTableBase), kIndirectionTableSize,
      xe::memory::AllocationType::kReserve,
      xe::memory::PageAccess::kReadWrite));
  if (!indirection_table_base_) {
    XELOGE("Unable to allocate code cache indirection table");
    XELOGE(
        "This is likely because the {:X}-{:X} range is in use by some other "
        "system DLL",
        static_cast<uint64_t>(kIndirectionTableBase),
        kIndirectionTableBase + kIndirectionTableSize);
    return false;
  }
  indirection_table_actual_base_ = kIndirectionTableBase;
#endif

  // Create mmap file. This allows us to share the code cache with the debugger.
  file_name_ = fmt::format("xenia_code_cache");
  mapping_ = xe::memory::CreateFileMappingHandle(
      file_name_, kGeneratedCodeSize, xe::memory::PageAccess::kExecuteReadWrite,
      false);
  if (mapping_ == xe::memory::kFileMappingHandleInvalid) {
    XELOGE("Unable to create code cache mmap");
    return false;
  }

  // Map generated code region into the file. Pages are committed as required.
  if (xe::memory::IsWritableExecutableMemoryPreferred()) {
#if XE_PLATFORM_MAC && XE_ARCH_ARM64
    // On macOS ARM64, use MAP_JIT allocation for proper JIT memory
    // This allows both write and execute permissions with pthread_jit_write_protect_np() control
    // CRITICAL: Use AllocFixed instead of MapFileView to ensure MAP_JIT memory
    generated_code_execute_base_ =
        reinterpret_cast<uint8_t*>(xe::memory::AllocFixed(
            reinterpret_cast<void*>(kGeneratedCodeExecuteBase),
            kGeneratedCodeSize, xe::memory::AllocationType::kReserveCommit,
            xe::memory::PageAccess::kExecuteReadWrite));
    if (!generated_code_execute_base_) {
      XELOGW("Fixed address mapping for generated code failed, trying OS-chosen address");
      generated_code_execute_base_ =
          reinterpret_cast<uint8_t*>(xe::memory::AllocFixed(
              nullptr, kGeneratedCodeSize, 
              xe::memory::AllocationType::kReserveCommit,
              xe::memory::PageAccess::kExecuteReadWrite));
    }
    generated_code_write_base_ = generated_code_execute_base_;
    if (!generated_code_execute_base_ || !generated_code_write_base_) {
      XELOGE("Unable to allocate code cache generated code storage");
      return false;
    }
    // On macOS ARM64, verify the memory is properly allocated for MAP_JIT
#else
    generated_code_execute_base_ =
        reinterpret_cast<uint8_t*>(xe::memory::MapFileView(
            mapping_, reinterpret_cast<void*>(kGeneratedCodeExecuteBase),
            kGeneratedCodeSize, xe::memory::PageAccess::kExecuteReadWrite, 0));
    generated_code_write_base_ = generated_code_execute_base_;
    if (!generated_code_execute_base_ || !generated_code_write_base_) {
      XELOGE("Unable to allocate code cache generated code storage");
      XELOGE(
          "This is likely because the {:X}-{:X} range is in use by some other "
          "system DLL",
          uint64_t(kGeneratedCodeExecuteBase),
          uint64_t(kGeneratedCodeExecuteBase + kGeneratedCodeSize));
      return false;
    }
#endif
  } else {
#if XE_PLATFORM_MAC && XE_ARCH_ARM64
    // On macOS ARM64, try OS-chosen addresses if fixed addresses fail
    generated_code_execute_base_ =
        reinterpret_cast<uint8_t*>(xe::memory::MapFileView(
            mapping_, reinterpret_cast<void*>(kGeneratedCodeExecuteBase),
            kGeneratedCodeSize, xe::memory::PageAccess::kExecuteReadOnly, 0));
    if (!generated_code_execute_base_) {
      XELOGW("Fixed address mapping for execute code failed, trying OS-chosen address");
      generated_code_execute_base_ =
          reinterpret_cast<uint8_t*>(xe::memory::MapFileView(
              mapping_, nullptr, kGeneratedCodeSize, 
              xe::memory::PageAccess::kExecuteReadOnly, 0));
    }
    
    generated_code_write_base_ =
        reinterpret_cast<uint8_t*>(xe::memory::MapFileView(
            mapping_, reinterpret_cast<void*>(kGeneratedCodeWriteBase),
            kGeneratedCodeSize, xe::memory::PageAccess::kReadWrite, 0));
    if (!generated_code_write_base_) {
      XELOGW("Fixed address mapping for write code failed, trying OS-chosen address");
      generated_code_write_base_ =
          reinterpret_cast<uint8_t*>(xe::memory::MapFileView(
              mapping_, nullptr, kGeneratedCodeSize, 
              xe::memory::PageAccess::kReadWrite, 0));
    }
    
    if (!generated_code_execute_base_ || !generated_code_write_base_) {
      XELOGE("Unable to allocate code cache generated code storage");
      return false;
    }
#else
    generated_code_execute_base_ =
        reinterpret_cast<uint8_t*>(xe::memory::MapFileView(
            mapping_, reinterpret_cast<void*>(kGeneratedCodeExecuteBase),
            kGeneratedCodeSize, xe::memory::PageAccess::kExecuteReadOnly, 0));
    generated_code_write_base_ =
        reinterpret_cast<uint8_t*>(xe::memory::MapFileView(
            mapping_, reinterpret_cast<void*>(kGeneratedCodeWriteBase),
            kGeneratedCodeSize, xe::memory::PageAccess::kReadWrite, 0));
    if (!generated_code_execute_base_ || !generated_code_write_base_) {
      XELOGE("Unable to allocate code cache generated code storage");
      XELOGE(
          "This is likely because the {:X}-{:X} and {:X}-{:X} ranges are in "
          "use by some other system DLL",
          uint64_t(kGeneratedCodeExecuteBase),
          uint64_t(kGeneratedCodeExecuteBase + kGeneratedCodeSize),
          uint64_t(kGeneratedCodeWriteBase),
          uint64_t(kGeneratedCodeWriteBase + kGeneratedCodeSize));
      return false;
    }
#endif
  }

  // Preallocate the function map to a large, reasonable size.
  generated_code_map_.reserve(kMaximumFunctionCount);

  return true;
}

void A64CodeCache::set_indirection_default(uint32_t default_value) {
#if XE_PLATFORM_MAC && XE_ARCH_ARM64
  // On macOS ARM64, we extend 32-bit values to 64-bit
  indirection_default_value_ = default_value;
#else
  indirection_default_value_ = default_value;
#endif
}

#if XE_PLATFORM_MAC && XE_ARCH_ARM64
void A64CodeCache::set_indirection_default_64(uint64_t default_value) {
  indirection_default_value_ = default_value;
}
#endif

void A64CodeCache::AddIndirection(uint32_t guest_address,
                                  uint32_t host_address) {
#if XE_PLATFORM_MAC && XE_ARCH_ARM64
  // On macOS ARM64, delegate to the 64-bit version
  AddIndirection64(guest_address, host_address);
#else
  if (!indirection_table_base_) {
    return;
  }

  uint32_t* indirection_slot = reinterpret_cast<uint32_t*>(
      indirection_table_base_ + (guest_address - kIndirectionTableBase));
  *indirection_slot = host_address;
#endif
}

#if XE_PLATFORM_MAC && XE_ARCH_ARM64
void A64CodeCache::AddIndirection64(uint32_t guest_address,
                                    uint64_t host_address) {
  if (!indirection_table_base_) {
    return;
  }

  // Calculate offset from the logical base (0x80000000), not from actual table address
  uintptr_t guest_offset = (guest_address - kIndirectionTableBase) * 2;  // 8-byte entries
  uint64_t* indirection_slot = reinterpret_cast<uint64_t*>(
      indirection_table_base_ + guest_offset);
  *indirection_slot = host_address;
}
#endif

void A64CodeCache::CommitExecutableRange(uint32_t guest_low,
                                         uint32_t guest_high) {
  if (!indirection_table_base_) {
    XELOGE("CommitExecutableRange: indirection_table_base_ is null!");
    return;
  }

#if XE_PLATFORM_MAC && XE_ARCH_ARM64
  // On macOS ARM64: use offset-based addressing from guest base (0x80000000)
  static const uintptr_t kGuestAddressBase = 0x80000000;
  
  // Calculate offsets from the guest address base, not the table base
  if (guest_low < kGuestAddressBase) {
    XELOGE("CommitExecutableRange: guest_low 0x{:08X} is below guest base 0x{:08X}", 
           guest_low, kGuestAddressBase);
    return;
  }
  
  uint32_t start_offset = (guest_low - kGuestAddressBase) * 2;  // 8-byte entries
  uint32_t size = (guest_high - guest_low) * 2;
  
  // Sanity check bounds; the table should fully cover the XEX guest range now.
  if (start_offset + size > kIndirectionTableSize) {
    XELOGE("CommitExecutableRange: range [0x{:08X}, 0x{:08X}) exceeds table (size 0x{:X})",
           guest_low, guest_high, (unsigned)kIndirectionTableSize);
    return;
  }
  
  // The memory should already be allocated, just fill with default value
  void* target_memory = indirection_table_base_ + start_offset;
  uint64_t* p = reinterpret_cast<uint64_t*>(target_memory);
  uint32_t entry_count = size / 8;  // 8 bytes per entry
  for (uint32_t i = 0; i < entry_count; i++) {
    p[i] = indirection_default_value_;
  }
#else
  // Other platforms: use 32-bit entries
  uint32_t start_offset = (guest_low - kIndirectionTableBase);
  uint32_t size = (guest_high - guest_low);

  xe::memory::AllocFixed(
      indirection_table_base_ + start_offset, size,
      xe::memory::AllocationType::kCommit,
      xe::memory::PageAccess::kReadWrite);

  uint32_t* p = reinterpret_cast<uint32_t*>(indirection_table_base_);
  for (uint32_t address = guest_low; address < guest_high; address += 4) {
    p[(address - kIndirectionTableBase) / 4] = indirection_default_value_;
  }
#endif
}

void A64CodeCache::PlaceHostCode(uint32_t guest_address, void* machine_code,
                                 const EmitFunctionInfo& func_info,
                                 void*& code_execute_address_out,
                                 void*& code_write_address_out) {
  // Same for now. We may use different pools or whatnot later on, like when
  // we only want to place guest code in a serialized cache on disk.
  PlaceGuestCode(guest_address, machine_code, func_info, nullptr,
                 code_execute_address_out, code_write_address_out);
}

void A64CodeCache::PlaceGuestCode(uint32_t guest_address, void* machine_code,
                                  const EmitFunctionInfo& func_info,
                                  GuestFunction* function_info,
                                  void*& code_execute_address_out,
                                  void*& code_write_address_out) {
  // Hold a lock while we bump the pointers up. This is important as the
  // unwind table requires entries AND code to be sorted in order.
  size_t low_mark;
  size_t high_mark;
  uint8_t* code_execute_address;
  UnwindReservation unwind_reservation;
  {
    auto global_lock = global_critical_region_.Acquire();

    low_mark = generated_code_offset_;

    // Reserve code.
    // Always move the code to land on 16b alignment.
    code_execute_address =
        generated_code_execute_base_ + generated_code_offset_;
    code_execute_address_out = code_execute_address;
    uint8_t* code_write_address =
        generated_code_write_base_ + generated_code_offset_;
    code_write_address_out = code_write_address;
    generated_code_offset_ += xe::round_up(func_info.code_size.total, 16);

    auto tail_write_address =
        generated_code_write_base_ + generated_code_offset_;

    // Reserve unwind info.
    // We go on the high size of the unwind info as we don't know how big we
    // need it, and a few extra bytes of padding isn't the worst thing.
    unwind_reservation = RequestUnwindReservation(generated_code_write_base_ +
                                                  generated_code_offset_);
    generated_code_offset_ += xe::round_up(unwind_reservation.data_size, 16);

    auto end_write_address =
        generated_code_write_base_ + generated_code_offset_;

    high_mark = generated_code_offset_;

    // Store in map. It is maintained in sorted order of host PC dependent on
    // us also being append-only.
    generated_code_map_.emplace_back(
        (uint64_t(code_execute_address - generated_code_execute_base_) << 32) |
            generated_code_offset_,
        function_info);

    // TODO(DrChat): The following code doesn't really need to be under the
    // global lock except for PlaceCode (but it depends on the previous code
    // already being ran)

    // If we are going above the high water mark of committed memory, commit
    // some more. It's ok if multiple threads do this, as redundant commits
    // aren't harmful.
    size_t old_commit_mark, new_commit_mark;
    do {
      old_commit_mark = generated_code_commit_mark_;
      if (high_mark <= old_commit_mark) break;

      new_commit_mark = old_commit_mark + 16_MiB;
      if (generated_code_execute_base_ == generated_code_write_base_) {
        xe::memory::AllocFixed(generated_code_execute_base_, new_commit_mark,
                               xe::memory::AllocationType::kCommit,
                               xe::memory::PageAccess::kExecuteReadWrite);
      } else {
        xe::memory::AllocFixed(generated_code_execute_base_, new_commit_mark,
                               xe::memory::AllocationType::kCommit,
                               xe::memory::PageAccess::kExecuteReadOnly);
        xe::memory::AllocFixed(generated_code_write_base_, new_commit_mark,
                               xe::memory::AllocationType::kCommit,
                               xe::memory::PageAccess::kReadWrite);
      }
    } while (generated_code_commit_mark_.compare_exchange_weak(
        old_commit_mark, new_commit_mark));

    // Copy code using platform-specific method that handles JIT protection
    CopyMachineCode(code_write_address, machine_code, func_info.code_size.total);

    // Fill unused slots with 0x00
#if XE_PLATFORM_MAC && defined(__aarch64__)
    // On MAP_JIT memory, ensure write mode is enabled before memset
    pthread_jit_write_protect_np(0);
#endif
    if (end_write_address > tail_write_address) {
      std::memset(tail_write_address, 0x00,
                  static_cast<size_t>(end_write_address - tail_write_address));
    }
#if XE_PLATFORM_MAC && defined(__aarch64__)
    // Restore execute mode after memset
    pthread_jit_write_protect_np(1);
#endif

    // Notify subclasses of placed code.
    PlaceCode(guest_address, machine_code, func_info, code_execute_address,
              unwind_reservation);
  }

  // Now that everything is ready, fix up the indirection table.
  // Note that we do support code that doesn't have an indirection fixup, so
  // ignore those when we see them.
  if (guest_address && indirection_table_base_) {
#if XE_PLATFORM_MAC && XE_ARCH_ARM64
    // On ARM64 Mac, map guest addresses to table offsets using logical base
    // kIndirectionTableBase remains 0x80000000 for calculation purposes
    
    // Calculate offset from the logical guest base (0x80000000)
    if (guest_address < kIndirectionTableBase) {
      XELOGE("A64CodeCache::PlaceGuestCode: ERROR - guest_address 0x{:08X} is below logical base 0x{:08X}!", 
             guest_address, static_cast<uint32_t>(kIndirectionTableBase));
      return;
    }
    
    // Debug the calculation step by step
    uintptr_t guest_diff = guest_address - kIndirectionTableBase;
    uintptr_t guest_offset = guest_diff * 2;  // 8-byte entries
    uintptr_t slot_address = reinterpret_cast<uintptr_t>(indirection_table_base_) + guest_offset;
    uint64_t* indirection_slot = reinterpret_cast<uint64_t*>(slot_address);
    
    // Check if the slot address is within bounds
    uintptr_t table_end = reinterpret_cast<uintptr_t>(indirection_table_base_) + kIndirectionTableSize;
    if (slot_address >= table_end) {
      XELOGE("A64CodeCache::PlaceGuestCode: slot 0x{:016X} beyond table end 0x{:016X}",
             slot_address, table_end);
      return;
    }
    
    *indirection_slot = reinterpret_cast<uint64_t>(code_execute_address);
#else
    uint32_t* indirection_slot = reinterpret_cast<uint32_t*>(
        indirection_table_base_ + (guest_address - kIndirectionTableBase));
    *indirection_slot =
        uint32_t(reinterpret_cast<uint64_t>(code_execute_address));
#endif
  }
}

uint32_t A64CodeCache::PlaceData(const void* data, size_t length) {
  // Hold a lock while we bump the pointers up.
  size_t high_mark;
  uint8_t* data_address = nullptr;
  {
    auto global_lock = global_critical_region_.Acquire();

    // Reserve code.
    // Always move the code to land on 16b alignment.
    data_address = generated_code_write_base_ + generated_code_offset_;
    generated_code_offset_ += xe::round_up(length, 16);

    high_mark = generated_code_offset_;
  }

  // If we are going above the high water mark of committed memory, commit some
  // more. It's ok if multiple threads do this, as redundant commits aren't
  // harmful.
  size_t old_commit_mark, new_commit_mark;
  do {
    old_commit_mark = generated_code_commit_mark_;
    if (high_mark <= old_commit_mark) break;

    new_commit_mark = old_commit_mark + 16_MiB;
    if (generated_code_execute_base_ == generated_code_write_base_) {
      xe::memory::AllocFixed(generated_code_execute_base_, new_commit_mark,
                             xe::memory::AllocationType::kCommit,
                             xe::memory::PageAccess::kExecuteReadWrite);
    } else {
      xe::memory::AllocFixed(generated_code_execute_base_, new_commit_mark,
                             xe::memory::AllocationType::kCommit,
                             xe::memory::PageAccess::kExecuteReadOnly);
      xe::memory::AllocFixed(generated_code_write_base_, new_commit_mark,
                             xe::memory::AllocationType::kCommit,
                             xe::memory::PageAccess::kReadWrite);
    }
  } while (generated_code_commit_mark_.compare_exchange_weak(old_commit_mark,
                                                             new_commit_mark));

  // Copy code.
  std::memcpy(data_address, data, length);

  return uint32_t(uintptr_t(data_address));
}

GuestFunction* A64CodeCache::LookupFunction(uint64_t host_pc) {
  uint32_t key = uint32_t(host_pc - kGeneratedCodeExecuteBase);
  void* fn_entry = std::bsearch(
      &key, generated_code_map_.data(), generated_code_map_.size() + 1,
      sizeof(std::pair<uint32_t, Function*>),
      [](const void* key_ptr, const void* element_ptr) {
        auto key = *reinterpret_cast<const uint32_t*>(key_ptr);
        auto element =
            reinterpret_cast<const std::pair<uint64_t, GuestFunction*>*>(
                element_ptr);
        if (key < (element->first >> 32)) {
          return -1;
        } else if (key > uint32_t(element->first)) {
          return 1;
        } else {
          return 0;
        }
      });
  if (fn_entry) {
    return reinterpret_cast<const std::pair<uint64_t, GuestFunction*>*>(
               fn_entry)
        ->second;
  } else {
    return nullptr;
  }
}

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
