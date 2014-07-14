/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_RUNTIME_RAW_MODULE_H_
#define ALLOY_RUNTIME_RAW_MODULE_H_

#include <string>

#include <alloy/runtime/module.h>

namespace alloy {
namespace runtime {

class RawModule : public Module {
 public:
  RawModule(Runtime* runtime);
  ~RawModule() override;

  int LoadFile(uint64_t base_address, const std::string& path);

  const std::string& name() const override { return name_; }

  bool ContainsAddress(uint64_t address) override;

 private:
  std::string name_;
  uint64_t base_address_;
  uint64_t low_address_;
  uint64_t high_address_;
};

}  // namespace runtime
}  // namespace alloy

#endif  // ALLOY_RUNTIME_RAW_MODULE_H_
