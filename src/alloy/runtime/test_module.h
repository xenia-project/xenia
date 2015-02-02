/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_RUNTIME_TEST_MODULE_H_
#define ALLOY_RUNTIME_TEST_MODULE_H_

#include <functional>
#include <memory>
#include <string>

#include "alloy/backend/assembler.h"
#include "alloy/compiler/compiler.h"
#include "alloy/hir/hir_builder.h"
#include "alloy/runtime/module.h"

namespace alloy {
namespace runtime {

class TestModule : public Module {
 public:
  TestModule(Runtime* runtime, const std::string& name,
             std::function<bool(uint64_t)> contains_address,
             std::function<bool(hir::HIRBuilder&)> generate);
  ~TestModule() override;

  const std::string& name() const override { return name_; }

  bool ContainsAddress(uint64_t address) override;

  SymbolInfo::Status DeclareFunction(uint64_t address,
                                     FunctionInfo** out_symbol_info) override;

 private:
  std::string name_;
  std::function<bool(uint64_t)> contains_address_;
  std::function<bool(hir::HIRBuilder&)> generate_;

  std::unique_ptr<hir::HIRBuilder> builder_;
  std::unique_ptr<compiler::Compiler> compiler_;
  std::unique_ptr<backend::Assembler> assembler_;
};

}  // namespace runtime
}  // namespace alloy

#endif  // ALLOY_RUNTIME_TEST_MODULE_H_
