/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_RAW_MODULE_H_
#define XENIA_CPU_RAW_MODULE_H_

#include <string>

#include "xenia/cpu/module.h"

namespace xe {
namespace cpu {

class RawModule : public Module {
 public:
  explicit RawModule(Processor* processor);
  ~RawModule() override;

  bool LoadFile(uint32_t base_address, const std::wstring& path);

  // Set address range if you've already allocated memory and placed code
  // in it.
  void SetAddressRange(uint32_t base_address, uint32_t size);

  const std::string& name() const override { return name_; }
  void set_name(const std::string& name) { name_ = name; }

  bool ContainsAddress(uint32_t address) override;

 protected:
  std::unique_ptr<Function> CreateFunction(uint32_t address) override;

 private:
  std::string name_;
  uint32_t base_address_;
  uint32_t low_address_;
  uint32_t high_address_;
};

}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_RAW_MODULE_H_
