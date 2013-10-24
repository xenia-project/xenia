/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_PPC_REGISTERS_H_
#define XENIA_CPU_PPC_REGISTERS_H_

#include <xenia/common.h>


namespace xe {
namespace cpu {
namespace ppc {


typedef bool (*RegisterHandlesCallback)(void* context, uint32_t addr);
typedef uint64_t (*RegisterReadCallback)(void* context, uint32_t addr);
typedef void (*RegisterWriteCallback)(void* context, uint32_t addr,
                                      uint64_t value);


typedef struct {
  void* context;
  RegisterHandlesCallback handles;
  RegisterReadCallback read;
  RegisterWriteCallback write;
} RegisterAccessCallbacks;


}  // namespace ppc
}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_PPC_REGISTERS_H_
