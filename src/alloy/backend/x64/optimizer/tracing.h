/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_BACKEND_X64_OPTIMIZER_TRACING_H_
#define ALLOY_BACKEND_X64_OPTIMIZER_TRACING_H_

#include <alloy/backend/x64/tracing.h>


namespace alloy {
namespace backend {
namespace x64 {
namespace optimizer {

const uint32_t ALLOY_BACKEND_X64_OPTIMIZER =
    alloy::backend::x64::EventType::ALLOY_BACKEND_X64_OPTIMIZER;


class EventType {
public:
  enum {
    ALLOY_BACKEND_X64_OPTIMIZER_INIT        = ALLOY_BACKEND_X64_OPTIMIZER | (1),
    ALLOY_BACKEND_X64_OPTIMIZER_DEINIT      = ALLOY_BACKEND_X64_OPTIMIZER | (2),
  };

  typedef struct {
    static const uint32_t event_type = ALLOY_BACKEND_X64_OPTIMIZER_INIT;
  } Init;
  typedef struct {
    static const uint32_t event_type = ALLOY_BACKEND_X64_OPTIMIZER_DEINIT;
  } Deinit;
};


}  // namespace optimizer
}  // namespace x64
}  // namespace backend
}  // namespace alloy


#endif  // ALLOY_BACKEND_X64_OPTIMIZER_TRACING_H_
