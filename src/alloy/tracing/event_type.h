/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_TRACING_EVENT_TYPES_H_
#define ALLOY_TRACING_EVENT_TYPES_H_

#include <alloy/core.h>


namespace alloy {
namespace tracing {


class EventType {
public:
  enum {
    ALLOY                               = (0 << 31),
    ALLOY_TRACE_INIT                    = ALLOY | (1),
    ALLOY_TRACE_EOF                     = ALLOY | (2),

    ALLOY_BACKEND                       = ALLOY | (1 << 26),
    ALLOY_COMPILER                      = ALLOY | (2 << 26),
    ALLOY_HIR                           = ALLOY | (3 << 26),
    ALLOY_FRONTEND                      = ALLOY | (4 << 26),
    ALLOY_RUNTIME                       = ALLOY | (5 << 26),

    USER                                = (1 << 31),
  };

  typedef struct {
    static const uint32_t event_type = ALLOY_TRACE_INIT;
  } TraceInit;
  typedef struct {
    static const uint32_t event_type = ALLOY_TRACE_EOF;
  } TraceEOF;
};


}  // namespace tracing
}  // namespace alloy


#endif  // ALLOY_TRACING_EVENT_TYPES_H_
