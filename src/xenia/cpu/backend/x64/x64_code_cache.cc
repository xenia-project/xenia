/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/x64/x64_code_cache.h"

#include <cstdlib>
#include <cstring>

#if ENABLE_VTUNE
#include "third_party/vtune/include/jitprofiling.h"
#pragma comment(lib, "../third_party/vtune/lib64/jitprofiling.lib")
#endif

#include "xenia/base/assert.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/cpu/function.h"
#include "xenia/cpu/module.h"

namespace xe {
namespace cpu {
namespace backend {
namespace x64 {

X64CodeCache::X64CodeCache() = default;

X64CodeCache::~X64CodeCache() {
  if (indirection_table_base_) {
    xe::memory::DeallocFixed(indirection_table_base_, 0,
                             xe::memory::DeallocationType::kRelease);
  }

  // Unmap all views and close mapping.
  if (mapping_) {
    xe::memory::UnmapFileView(mapping_, generated_code_base_,
                              kGeneratedCodeSize);
    xe::memory::CloseFileMappingHandle(mapping_);
    mapping_ = nullptr;
  }
}

bool X64CodeCache::Initialize() {
  indirection_table_base_ = reinterpret_cast<uint8_t*>(xe::memory::AllocFixed(
      reinterpret_cast<void*>(kIndirectionTableBase), kIndirectionTableSize,
      xe::memory::AllocationType::kReserve,
      xe::memory::PageAccess::kReadWrite));
  if (!indirection_table_base_) {
    XELOGE("Unable to allocate code cache indirection table");
    XELOGE(
        "This is likely because the %.8X-%.8X range is in use by some other "
        "system DLL",
        kIndirectionTableBase, kIndirectionTableBase + kIndirectionTableSize);
  }

  // Create mmap file. This allows us to share the code cache with the debugger.
  file_name_ = std::wstring(L"Local\\xenia_code_cache_") +
               std::to_wstring(Clock::QueryHostTickCount());
  mapping_ = xe::memory::CreateFileMappingHandle(
      file_name_, kGeneratedCodeSize, xe::memory::PageAccess::kExecuteReadWrite,
      false);
  if (!mapping_) {
    XELOGE("Unable to create code cache mmap");
    return false;
  }

  // Map generated code region into the file. Pages are committed as required.
  generated_code_base_ = reinterpret_cast<uint8_t*>(xe::memory::MapFileView(
      mapping_, reinterpret_cast<void*>(kGeneratedCodeBase), kGeneratedCodeSize,
      xe::memory::PageAccess::kExecuteReadWrite, 0));
  if (!generated_code_base_) {
    XELOGE("Unable to allocate code cache generated code storage");
    XELOGE(
        "This is likely because the %.8X-%.8X range is in use by some other "
        "system DLL",
        kGeneratedCodeBase, kGeneratedCodeBase + kGeneratedCodeSize);
    return false;
  }

  // Preallocate the function map to a large, reasonable size.
  generated_code_map_.reserve(kMaximumFunctionCount);

  return true;
}

void X64CodeCache::set_indirection_default(uint32_t default_value) {
  indirection_default_value_ = default_value;
}

void X64CodeCache::AddIndirection(uint32_t guest_address,
                                  uint32_t host_address) {
  if (!indirection_table_base_) {
    return;
  }

  uint32_t* indirection_slot = reinterpret_cast<uint32_t*>(
      indirection_table_base_ + (guest_address - kIndirectionTableBase));
  *indirection_slot = host_address;
}

void X64CodeCache::CommitExecutableRange(uint32_t guest_low,
                                         uint32_t guest_high) {
  if (!indirection_table_base_) {
    return;
  }

  // Commit the memory.
  xe::memory::AllocFixed(
      indirection_table_base_ + (guest_low - kIndirectionTableBase),
      guest_high - guest_low, xe::memory::AllocationType::kCommit,
      xe::memory::PageAccess::kExecuteReadWrite);

  // Fill memory with the default value.
  uint32_t* p = reinterpret_cast<uint32_t*>(indirection_table_base_);
  for (uint32_t address = guest_low; address < guest_high; ++address) {
    p[(address - kIndirectionTableBase) / 4] = indirection_default_value_;
  }
}

void* X64CodeCache::PlaceHostCode(uint32_t guest_address, void* machine_code,
                                  size_t code_size, size_t stack_size) {
  // Same for now. We may use different pools or whatnot later on, like when
  // we only want to place guest code in a serialized cache on disk.
  return PlaceGuestCode(guest_address, machine_code, code_size, stack_size,
                        nullptr);
}

void* X64CodeCache::PlaceGuestCode(uint32_t guest_address, void* machine_code,
                                   size_t code_size, size_t stack_size,
                                   GuestFunction* function_info) {
  // Hold a lock while we bump the pointers up. This is important as the
  // unwind table requires entries AND code to be sorted in order.
  size_t low_mark;
  size_t high_mark;
  uint8_t* code_address = nullptr;
  UnwindReservation unwind_reservation;
  {
    auto global_lock = global_critical_region_.Acquire();

    low_mark = generated_code_offset_;

    // Reserve code.
    // Always move the code to land on 16b alignment.
    code_address = generated_code_base_ + generated_code_offset_;
    generated_code_offset_ += xe::round_up(code_size, 16);

    // Reserve unwind info.
    // We go on the high size of the unwind info as we don't know how big we
    // need it, and a few extra bytes of padding isn't the worst thing.
    unwind_reservation =
        RequestUnwindReservation(generated_code_base_ + generated_code_offset_);
    generated_code_offset_ += unwind_reservation.data_size;

    high_mark = generated_code_offset_;

    // Store in map. It is maintained in sorted order of host PC dependent on
    // us also being append-only.
    generated_code_map_.emplace_back(
        (uint64_t(code_address - generated_code_base_) << 32) |
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

      new_commit_mark = old_commit_mark + 16 * 1024 * 1024;
      xe::memory::AllocFixed(generated_code_base_, new_commit_mark,
                             xe::memory::AllocationType::kCommit,
                             xe::memory::PageAccess::kExecuteReadWrite);
    } while (generated_code_commit_mark_.compare_exchange_weak(
        old_commit_mark, new_commit_mark));

    // Copy code.
    std::memcpy(code_address, machine_code, code_size);

    // Fill unused slots with 0xCC
    std::memset(
        code_address + code_size, 0xCC,
        xe::round_up(code_size + unwind_reservation.data_size, 16) - code_size);

    // Notify subclasses of placed code.
    PlaceCode(guest_address, machine_code, code_size, stack_size, code_address,
              unwind_reservation);
  }

#if ENABLE_VTUNE
  if (iJIT_IsProfilingActive() == iJIT_SAMPLING_ON) {
    std::string method_name;
    if (function_info && function_info->name().size() != 0) {
      method_name = function_info->name();
    } else {
      method_name = xe::format_string("sub_%.8X", guest_address);
    }

    iJIT_Method_Load_V2 method = {0};
    method.method_id = iJIT_GetNewMethodID();
    method.method_load_address = code_address;
    method.method_size = uint32_t(code_size);
    method.method_name = const_cast<char*>(method_name.data());
    method.module_name = function_info
                             ? (char*)function_info->module()->name().c_str()
                             : nullptr;
    iJIT_NotifyEvent(iJVM_EVENT_TYPE_METHOD_LOAD_FINISHED_V2, (void*)&method);
  }
#endif

  // Now that everything is ready, fix up the indirection table.
  // Note that we do support code that doesn't have an indirection fixup, so
  // ignore those when we see them.
  if (guest_address && indirection_table_base_) {
    uint32_t* indirection_slot = reinterpret_cast<uint32_t*>(
        indirection_table_base_ + (guest_address - kIndirectionTableBase));
    *indirection_slot = uint32_t(reinterpret_cast<uint64_t>(code_address));
  }

  return code_address;
}

uint32_t X64CodeCache::PlaceData(const void* data, size_t length) {
  // Hold a lock while we bump the pointers up.
  size_t high_mark;
  uint8_t* data_address = nullptr;
  {
    auto global_lock = global_critical_region_.Acquire();

    // Reserve code.
    // Always move the code to land on 16b alignment.
    data_address = generated_code_base_ + generated_code_offset_;
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

    new_commit_mark = old_commit_mark + 16 * 1024 * 1024;
    xe::memory::AllocFixed(generated_code_base_, new_commit_mark,
                           xe::memory::AllocationType::kCommit,
                           xe::memory::PageAccess::kExecuteReadWrite);
  } while (generated_code_commit_mark_.compare_exchange_weak(old_commit_mark,
                                                             new_commit_mark));

  // Copy code.
  std::memcpy(data_address, data, length);

  return uint32_t(uintptr_t(data_address));
}

GuestFunction* X64CodeCache::LookupFunction(uint64_t host_pc) {
  uint32_t key = uint32_t(host_pc - kGeneratedCodeBase);
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

}  // namespace x64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
