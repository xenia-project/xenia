/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/runtime/raw_module.h>

#include <poly/platform.h>
#include <poly/string.h>

namespace alloy {
namespace runtime {

RawModule::RawModule(Runtime* runtime)
    : Module(runtime), base_address_(0), low_address_(0), high_address_(0) {}

RawModule::~RawModule() {}

int RawModule::LoadFile(uint64_t base_address, const std::wstring& path) {
  auto fixed_path = poly::to_string(poly::fix_path_separators(path));
  FILE* file = fopen(fixed_path.c_str(), "rb");
  fseek(file, 0, SEEK_END);
  size_t file_length = ftell(file);
  fseek(file, 0, SEEK_SET);

  // Allocate memory.
  // Since we have no real heap just load it wherever.
  base_address_ = base_address;
  uint8_t* p = memory_->Translate(base_address_);
  memset(p, 0, file_length);

  // Read into memory.
  fread(p, file_length, 1, file);

  fclose(file);

  // Setup debug info.
  auto last_slash = fixed_path.find_last_of(poly::path_separator);
  if (last_slash != std::string::npos) {
    name_ = fixed_path.substr(last_slash + 1);
  } else {
    name_ = fixed_path;
  }
  // TODO(benvanik): debug info

  low_address_ = base_address;
  high_address_ = base_address + file_length;
  return 0;
}

bool RawModule::ContainsAddress(uint64_t address) {
  return address >= low_address_ && address < high_address_;
}

}  // namespace runtime
}  // namespace alloy
