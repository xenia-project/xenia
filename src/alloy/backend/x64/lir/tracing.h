/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_BACKEND_X64_LIR_TRACING_H_
#define ALLOY_BACKEND_X64_LIR_TRACING_H_

#include <alloy/backend/x64/tracing.h>


namespace alloy {
namespace backend {
namespace x64 {
namespace lir {

const uint32_t ALLOY_BACKEND_X64_LIR =
    alloy::backend::x64::EventType::ALLOY_BACKEND_X64_LIR;


class EventType {
public:
  enum {
    ALLOY_BACKEND_X64_LIR_FOO             = ALLOY_BACKEND_X64_LIR | (1),
  };
};


}  // namespace lir
}  // namespace x64
}  // namespace backend
}  // namespace alloy


#endif  // ALLOY_BACKEND_X64_LIR_TRACING_H_
