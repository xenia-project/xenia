/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_CODE_UNIT_BUILDER_H_
#define XENIA_CPU_CODE_UNIT_BUILDER_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/core/memory.h>
#include <xenia/cpu/sdb.h>
#include <xenia/kernel/export.h>


namespace xe {
namespace cpu {


class CodeUnitBuilder {
public:
  virtual ~CodeUnitBuilder() {
    xe_memory_release(memory_);
  }

  virtual int Init(const char* module_name, const char* module_path) = 0;
  virtual int MakeFunction(sdb::FunctionSymbol* symbol) = 0;
  virtual int Finalize() = 0;
  virtual void Reset() = 0;

  // TODO(benvanik): write to file/etc

protected:
  CodeUnitBuilder(xe_memory_ref memory) {
    memory_ = xe_memory_retain(memory);
  }

  xe_memory_ref           memory_;
};


}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_CODE_UNIT_BUILDER_H_
