/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/raw_module.h"

#include "xenia/base/filesystem.h"
#include "xenia/base/platform.h"
#include "xenia/base/string.h"

namespace xe {
namespace cpu {

RawModule::RawModule(Processor* processor)
    : Module(processor), base_address_(0), low_address_(0), high_address_(0) {}

RawModule::~RawModule() {}

bool RawModule::LoadFile(uint32_t base_address, const std::wstring& path) {
  auto fixed_path = xe::fix_path_separators(path);
  FILE* file = xe::filesystem::OpenFile(fixed_path, "rb");
  fseek(file, 0, SEEK_END);
  uint32_t file_length = static_cast<uint32_t>(ftell(file));
  fseek(file, 0, SEEK_SET);

  // Allocate memory.
  // Since we have no real heap just load it wherever.
  base_address_ = base_address;
  memory_->LookupHeap(base_address_)
      ->AllocFixed(base_address_, file_length, 0,
                   kMemoryAllocationReserve | kMemoryAllocationCommit,
                   kMemoryProtectRead | kMemoryProtectWrite);
  uint8_t* p = memory_->TranslateVirtual(base_address_);

  // Read into memory.
  fread(p, file_length, 1, file);

  fclose(file);

  // Setup debug info.
  auto last_slash = fixed_path.find_last_of(xe::path_separator);
  if (last_slash != std::string::npos) {
    name_ = xe::to_string(fixed_path.substr(last_slash + 1));
  } else {
    name_ = xe::to_string(fixed_path);
  }
  // TODO(benvanik): debug info

  low_address_ = base_address;
  high_address_ = base_address + file_length;
  return true;
}

bool RawModule::ContainsAddress(uint32_t address) {
  return address >= low_address_ && address < high_address_;
}

}  // namespace cpu
}  // namespace xe
