/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/runtime/raw_module.h>

using namespace alloy;
using namespace alloy::runtime;


RawModule::RawModule(Memory* memory) :
    name_(0),
    base_address_(0), low_address_(0), high_address_(0),
    Module(memory) {
}

RawModule::~RawModule() {
  if (base_address_) {
    memory_->HeapFree(base_address_, high_address_ - low_address_);
  }
  xe_free(name_);
}

int RawModule::LoadFile(uint64_t base_address, const char* path) {
  FILE* file = fopen(path, "rb");
  fseek(file, 0, SEEK_END);
  size_t file_length = ftell(file);
  fseek(file, 0, SEEK_SET);

  // Allocate memory.
  base_address_ = memory_->HeapAlloc(
      base_address, file_length, MEMORY_FLAG_ZERO);
  if (!base_address_) {
    fclose(file);
    return 1;
  }

  // Read into memory.
  uint8_t* p = memory_->Translate(base_address_);
  fread(p, file_length, 1, file);

  fclose(file);

  // Setup debug info.
  const char* name = xestrrchra(path, XE_PATH_SEPARATOR) + 1;
  name_ = xestrdupa(name);
  // TODO(benvanik): debug info

  low_address_ = base_address;
  high_address_ = base_address + file_length;
  return 0;
}

bool RawModule::ContainsAddress(uint64_t address) {
  return address >= low_address_ && address < high_address_;
}
