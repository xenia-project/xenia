/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_FUNCTION_TABLE_H_
#define XENIA_CPU_FUNCTION_TABLE_H_

#include <xenia/core.h>

#include <xenia/cpu/ppc.h>


namespace xe {
namespace cpu {


typedef void (*FunctionPointer)(xe_ppc_state_t*, uint64_t);


class FunctionTable {
public:
  FunctionTable();
  ~FunctionTable();

  int AddCodeRange(uint32_t low_address, uint32_t high_address);
  FunctionPointer BeginAddFunction(uint32_t address);
  int AddFunction(uint32_t address, FunctionPointer ptr);
  FunctionPointer GetFunction(uint32_t address);

private:
  typedef std::tr1::unordered_map<uint32_t, FunctionPointer> FunctionMap;
  FunctionMap map_;
};


}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_FUNCTION_TABLE_H_
