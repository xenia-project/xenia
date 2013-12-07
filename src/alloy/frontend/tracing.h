/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_FRONTEND_TRACING_H_
#define ALLOY_FRONTEND_TRACING_H_

#include <alloy/tracing/tracing.h>
#include <alloy/tracing/event_type.h>


namespace alloy {
namespace frontend {

const uint32_t ALLOY_FRONTEND = alloy::tracing::EventType::ALLOY_FRONTEND;


class EventType {
public:
  enum {
    ALLOY_FRONTEND_INIT                 = ALLOY_FRONTEND | (1),
    ALLOY_FRONTEND_DEINIT               = ALLOY_FRONTEND | (2),
  };

  typedef struct {
    static const uint32_t event_type = ALLOY_FRONTEND_INIT;
  } Init;
  typedef struct {
    static const uint32_t event_type = ALLOY_FRONTEND_DEINIT;
  } Deinit;
};


}  // namespace frontend
}  // namespace alloy


#endif  // ALLOY_FRONTEND_TRACING_H_
