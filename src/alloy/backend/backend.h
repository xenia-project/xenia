/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_BACKEND_BACKEND_H_
#define ALLOY_BACKEND_BACKEND_H_

#include <memory>

#include <alloy/core.h>
#include <alloy/backend/machine_info.h>

namespace alloy {
namespace runtime {
class Runtime;
}  // namespace runtime
}  // namespace alloy

namespace alloy {
namespace backend {

class Assembler;

class Backend {
 public:
  Backend(runtime::Runtime* runtime);
  virtual ~Backend();

  runtime::Runtime* runtime() const { return runtime_; }
  const MachineInfo* machine_info() const { return &machine_info_; }

  virtual int Initialize();

  virtual void* AllocThreadData();
  virtual void FreeThreadData(void* thread_data);

  virtual std::unique_ptr<Assembler> CreateAssembler() = 0;

 protected:
  runtime::Runtime* runtime_;
  MachineInfo machine_info_;
};

}  // namespace backend
}  // namespace alloy

#endif  // ALLOY_BACKEND_BACKEND_H_
