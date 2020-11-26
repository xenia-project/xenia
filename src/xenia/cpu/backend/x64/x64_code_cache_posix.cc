/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2017 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/x64/x64_code_cache.h"

namespace xe {
namespace cpu {
namespace backend {
namespace x64 {

class PosixX64CodeCache : public X64CodeCache {
 public:
  PosixX64CodeCache();
  ~PosixX64CodeCache() override;

  bool Initialize() override;

  void* LookupUnwindInfo(uint64_t host_pc) override { return nullptr; }

 private:
  /*
  UnwindReservation RequestUnwindReservation(uint8_t* entry_address) override;
  void PlaceCode(uint32_t guest_address, void* machine_code, size_t code_size,
                 size_t stack_size, void* code_execute_address,
                 UnwindReservation unwind_reservation) override;

  void InitializeUnwindEntry(uint8_t* unwind_entry_address,
                             size_t unwind_table_slot, void* code_address,
                             size_t code_size, size_t stack_size);
  */
};

std::unique_ptr<X64CodeCache> X64CodeCache::Create() {
  return std::make_unique<PosixX64CodeCache>();
}

PosixX64CodeCache::PosixX64CodeCache() = default;
PosixX64CodeCache::~PosixX64CodeCache() = default;

bool PosixX64CodeCache::Initialize() { return X64CodeCache::Initialize(); }

}  // namespace x64
}  // namespace backend
}  // namespace cpu
}  // namespace xe