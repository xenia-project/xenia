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

#include <alloy/runtime/module.h>


namespace alloy {
namespace runtime {


class RawModule : public Module {
public:
  RawModule(Memory* memory);
  virtual ~RawModule();

  int LoadFile(uint64_t base_address, const char* path);

  virtual const char* name() const { return name_; }

  virtual bool ContainsAddress(uint64_t address);

private:
  char*     name_;
  uint64_t  base_address_;
  uint64_t  low_address_;
  uint64_t  high_address_;
};


}  // namespace runtime
}  // namespace alloy


#endif  // ALLOY_RUNTIME_RAW_MODULE_H_
