/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_RUNTIME_REGISTER_ACCESS_H_
#define ALLOY_RUNTIME_REGISTER_ACCESS_H_

#include <alloy/core.h>


namespace alloy {
namespace runtime {

typedef bool (*RegisterHandlesCallback)(void* context, uint64_t addr);
typedef uint64_t (*RegisterReadCallback)(void* context, uint64_t addr);
typedef void (*RegisterWriteCallback)(void* context, uint64_t addr,
                                      uint64_t value);

typedef struct {
  void* context;
  RegisterHandlesCallback handles;
  RegisterReadCallback read;
  RegisterWriteCallback write;
} RegisterAccessCallbacks;


}  // namespace runtime
}  // namespace alloy


#endif  // ALLOY_RUNTIME_REGISTER_ACCESS_H_
