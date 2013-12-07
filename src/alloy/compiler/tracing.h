/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_COMPILER_TRACING_H_
#define ALLOY_COMPILER_TRACING_H_

#include <alloy/tracing/tracing.h>
#include <alloy/tracing/event_type.h>


namespace alloy {
namespace compiler {

const uint32_t ALLOY_COMPILER = alloy::tracing::EventType::ALLOY_COMPILER;


class EventType {
public:
  enum {
    ALLOY_COMPILER_INIT                 = ALLOY_COMPILER | (1),
    ALLOY_COMPILER_DEINIT               = ALLOY_COMPILER | (2),
  };

  typedef struct {
    static const uint32_t event_type = ALLOY_COMPILER_INIT;
  } Init;
  typedef struct {
    static const uint32_t event_type = ALLOY_COMPILER_DEINIT;
  } Deinit;
};


}  // namespace compiler
}  // namespace alloy


#endif  // ALLOY_COMPILER_TRACING_H_
