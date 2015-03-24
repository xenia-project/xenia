/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_RUNTIME_RAW_MODULE_H_
#define XENIA_RUNTIME_RAW_MODULE_H_

#include <string>

#include "xenia/cpu/runtime/module.h"

namespace xe {
namespace cpu {
namespace runtime {

class RawModule : public Module {
 public:
  RawModule(Runtime* runtime);
  ~RawModule() override;

  int LoadFile(uint64_t base_address, const std::wstring& path);

  const std::string& name() const override { return name_; }

  bool ContainsAddress(uint64_t address) override;

 private:
  std::string name_;
  uint64_t base_address_;
  uint64_t low_address_;
  uint64_t high_address_;
};

}  // namespace runtime
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_RUNTIME_RAW_MODULE_H_
