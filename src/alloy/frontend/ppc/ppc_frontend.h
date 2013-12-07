/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_FRONTEND_PPC_PPC_FRONTEND_H_
#define ALLOY_FRONTEND_PPC_PPC_FRONTEND_H_

#include <alloy/core.h>
#include <alloy/type_pool.h>

#include <alloy/frontend/frontend.h>


namespace alloy {
namespace frontend {
namespace ppc {

class PPCTranslator;

class PPCFrontend : public Frontend {
public:
  PPCFrontend(runtime::Runtime* runtime);
  virtual ~PPCFrontend();

  virtual int Initialize();

  virtual int DeclareFunction(
      runtime::FunctionInfo* symbol_info);
  virtual int DefineFunction(
      runtime::FunctionInfo* symbol_info,
      runtime::Function** out_function);

private:
  TypePool<PPCTranslator, PPCFrontend*> translator_pool_;
};


}  // namespace ppc
}  // namespace frontend
}  // namespace alloy


#endif  // ALLOY_FRONTEND_PPC_PPC_FRONTEND_H_
