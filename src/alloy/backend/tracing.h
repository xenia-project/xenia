/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_BACKEND_TRACING_H_
#define ALLOY_BACKEND_TRACING_H_

#include <alloy/tracing/tracing.h>
#include <alloy/tracing/event_type.h>


namespace alloy {
namespace backend {

const uint32_t ALLOY_BACKEND = alloy::tracing::EventType::ALLOY_BACKEND;


class EventType {
public:
  enum {
    ALLOY_BACKEND_IVM                   = ALLOY_BACKEND | (1 << 24),
    ALLOY_BACKEND_X64                   = ALLOY_BACKEND | (2 << 24),
  };
};


}  // namespace backend
}  // namespace alloy


#endif  // ALLOY_BACKEND_TRACING_H_
