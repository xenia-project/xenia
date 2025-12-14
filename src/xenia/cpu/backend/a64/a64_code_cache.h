/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_BACKEND_A64_A64_CODE_CACHE_H_
#define XENIA_CPU_BACKEND_A64_A64_CODE_CACHE_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "xenia/base/memory.h"
#include "xenia/base/mutex.h"
#include "xenia/cpu/backend/code_cache.h"

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

struct EmitFunctionInfo {
  struct _code_size {
    size_t prolog;
    size_t body;
    size_t epilog;
    size_t tail;
    size_t total;
  } code_size;
  size_t prolog_stack_alloc_offset;  // offset of instruction after stack alloc
  size_t stack_size;
};

class A64CodeCache : public CodeCache {
 public:
  ~A64CodeCache() override;

  static std::unique_ptr<A64CodeCache> Create();

  virtual bool Initialize();

  const std::filesystem::path& file_name() const override { return file_name_; }
  uintptr_t execute_base_address() const override {
    return kGeneratedCodeExecuteBase;
  }
  size_t total_size() const override { return kGeneratedCodeSize; }

  // TODO(benvanik): ELF serialization/etc
  // TODO(benvanik): keep track of code blocks
  // TODO(benvanik): padding/guards/etc

  bool has_indirection_table() { return indirection_table_base_ != nullptr; }
  void set_indirection_default(uint32_t default_value);
  void AddIndirection(uint32_t guest_address, uint32_t host_address);

  void CommitExecutableRange(uint32_t guest_low, uint32_t guest_high);

  void PlaceHostCode(uint32_t guest_address, void* machine_code,
                     const EmitFunctionInfo& func_info,
                     void*& code_execute_address_out,
                     void*& code_write_address_out);
  void PlaceGuestCode(uint32_t guest_address, void* machine_code,
                      const EmitFunctionInfo& func_info,
                      GuestFunction* function_info,
                      void*& code_execute_address_out,
                      void*& code_write_address_out);
  uint32_t PlaceData(const void* data, size_t length);

  GuestFunction* LookupFunction(uint64_t host_pc) override;

 protected:
  // All executable code falls within 0x80000000 to 0x9FFFFFFF, so we can
  // only map enough for lookups within that range.
  static const size_t kIndirectionTableSize = 0x1FFFFFFF;
  static const uintptr_t kIndirectionTableBase = 0x80000000;
  // The code range is 512MB, but we know the total code games will have is
  // pretty small (dozens of mb at most) and our expansion is reasonablish
  // so 256MB should be more than enough.
  static const size_t kGeneratedCodeSize = 0x0FFFFFFF;
  static const uintptr_t kGeneratedCodeExecuteBase = 0xA0000000;
  // Used for writing when PageAccess::kExecuteReadWrite is not supported.
  static const uintptr_t kGeneratedCodeWriteBase =
      kGeneratedCodeExecuteBase + kGeneratedCodeSize + 1;

  // This is picked to be high enough to cover whatever we can reasonably
  // expect. If we hit issues with this it probably means some corner case
  // in analysis triggering.
  static const size_t kMaximumFunctionCount = 100000;

  struct UnwindReservation {
    size_t data_size = 0;
    size_t table_slot = 0;
    uint8_t* entry_address = 0;
  };

  A64CodeCache();

  virtual UnwindReservation RequestUnwindReservation(uint8_t* entry_address) {
    return UnwindReservation();
  }
  virtual void PlaceCode(uint32_t guest_address, void* machine_code,
                         const EmitFunctionInfo& func_info,
                         void* code_execute_address,
                         UnwindReservation unwind_reservation) {}

  std::filesystem::path file_name_;
  xe::memory::FileMappingHandle mapping_ =
      xe::memory::kFileMappingHandleInvalid;

  // NOTE: the global critical region must be held when manipulating the offsets
  // or counts of anything, to keep the tables consistent and ordered.
  xe::global_critical_region global_critical_region_;

  // Value that the indirection table will be initialized with upon commit.
  uint32_t indirection_default_value_ = 0xFEEDF00D;

  // Fixed at kIndirectionTableBase in host space, holding 4 byte pointers into
  // the generated code table that correspond to the PPC functions in guest
  // space.
  uint8_t* indirection_table_base_ = nullptr;
  // Fixed at kGeneratedCodeExecuteBase and holding all generated code, growing
  // as needed.
  uint8_t* generated_code_execute_base_ = nullptr;
  // View of the memory that backs generated_code_execute_base_ when
  // PageAccess::kExecuteReadWrite is not supported, for writing the generated
  // code. Equals to generated_code_execute_base_ when it's supported.
  uint8_t* generated_code_write_base_ = nullptr;
  // Current offset to empty space in generated code.
  size_t generated_code_offset_ = 0;
  // Current high water mark of COMMITTED code.
  std::atomic<size_t> generated_code_commit_mark_ = {0};
  // Sorted map by host PC base offsets to source function info.
  // This can be used to bsearch on host PC to find the guest function.
  // The key is [start address | end address].
  std::vector<std::pair<uint64_t, GuestFunction*>> generated_code_map_;
};

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_A64_A64_CODE_CACHE_H_
