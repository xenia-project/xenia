/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_HIR_TRACING_H_
#define ALLOY_HIR_TRACING_H_

#include <alloy/tracing/tracing.h>
#include <alloy/tracing/event_type.h>


namespace alloy {
namespace hir {

const uint32_t ALLOY_HIR = alloy::tracing::EventType::ALLOY_HIR;


class EventType {
public:
  enum {
    ALLOY_HIR_FOO                       = ALLOY_HIR | (0),
  };
};


}  // namespace hir
}  // namespace alloy


#endif  // ALLOY_HIR_TRACING_H_
