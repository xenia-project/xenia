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

#include <alloy/core.h>


namespace alloy { namespace runtime { class Runtime; } }

namespace alloy {
namespace backend {

class Assembler;


class Backend {
public:
  Backend(runtime::Runtime* runtime);
  virtual ~Backend();

  virtual int Initialize();

  virtual void* AllocThreadData();
  virtual void FreeThreadData(void* thread_data);

  virtual Assembler* CreateAssembler() = 0;

protected:
  runtime::Runtime* runtime_;
};


}  // namespace backend
}  // namespace alloy


#endif  // ALLOY_BACKEND_BACKEND_H_
