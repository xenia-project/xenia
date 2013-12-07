/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_BACKEND_IVM_IVM_BACKEND_H_
#define ALLOY_BACKEND_IVM_IVM_BACKEND_H_

#include <alloy/core.h>

#include <alloy/backend/backend.h>


namespace alloy {
namespace backend {
namespace ivm {


#define ALLOY_HAS_IVM_BACKEND 1


class IVMBackend : public Backend {
public:
  IVMBackend(runtime::Runtime* runtime);
  virtual ~IVMBackend();

  virtual int Initialize();

  virtual Assembler* CreateAssembler();
};


}  // namespace ivm
}  // namespace backend
}  // namespace alloy


#endif  // ALLOY_BACKEND_IVM_IVM_BACKEND_H_
