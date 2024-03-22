/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/raw_module.h"

#include "xenia/base/filesystem.h"
#include "xenia/base/platform.h"
#include "xenia/base/string.h"
#include "xenia/cpu/function.h"
#include "xenia/cpu/processor.h"

namespace xe {
namespace cpu {

RawModule::RawModule(Processor* processor)
    : Module(processor), base_address_(0), low_address_(0), high_address_(0) {}

RawModule::~RawModule() {}

bool RawModule::LoadFile(uint32_t base_address,
                         const std::filesystem::path& path) {
  FILE* file = xe::filesystem::OpenFile(path, "rb");
  const uint32_t file_length =
      static_cast<uint32_t>(std::filesystem::file_size(path));

  // Allocate memory.
  // Since we have no real heap just load it wherever.
  base_address_ = base_address;
  auto heap = memory_->LookupHeap(base_address_);
  if (!heap ||
      !heap->AllocFixed(base_address_, file_length, 0,
                        kMemoryAllocationReserve | kMemoryAllocationCommit,
                        kMemoryProtectRead | kMemoryProtectWrite)) {
    return false;
  }

  uint8_t* p = memory_->TranslateVirtual(base_address_);

  // Read into memory.
  fread(p, file_length, 1, file);
  fclose(file);

  // Setup debug info.
  name_ = xe::path_to_utf8(path.filename());
  // TODO(benvanik): debug info

  low_address_ = base_address;
  high_address_ = base_address + file_length;

  // Notify backend about executable code.
  processor_->backend()->CommitExecutableRange(low_address_, high_address_);
  return true;
}

void RawModule::SetAddressRange(uint32_t base_address, uint32_t size) {
  base_address_ = base_address;
  low_address_ = base_address;
  high_address_ = base_address + size;

  // Notify backend about executable code.
  processor_->backend()->CommitExecutableRange(low_address_, high_address_);
}

bool RawModule::ContainsAddress(uint32_t address) {
  return address >= low_address_ && address < high_address_;
}

std::unique_ptr<Function> RawModule::CreateFunction(uint32_t address) {
  return std::unique_ptr<Function>(
      processor_->backend()->CreateGuestFunction(this, address));
}

}  // namespace cpu
}  // namespace xe
