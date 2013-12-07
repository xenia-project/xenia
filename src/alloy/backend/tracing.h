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
    ALLOY_BACKEND_INIT                  = ALLOY_BACKEND | (1),
    ALLOY_BACKEND_DEINIT                = ALLOY_BACKEND | (2),

    ALLOY_BACKEND_ASSEMBLER             = ALLOY_BACKEND | (1 << 25),
    ALLOY_BACKEND_ASSEMBLER_INIT        = ALLOY_BACKEND_ASSEMBLER | (1),
    ALLOY_BACKEND_ASSEMBLER_DEINIT      = ALLOY_BACKEND_ASSEMBLER | (2),
  };

  typedef struct {
    static const uint32_t event_type = ALLOY_BACKEND_INIT;
  } Init;
  typedef struct {
    static const uint32_t event_type = ALLOY_BACKEND_DEINIT;
  } Deinit;

  typedef struct {
    static const uint32_t event_type = ALLOY_BACKEND_ASSEMBLER_INIT;
  } AssemblerInit;
  typedef struct {
    static const uint32_t event_type = ALLOY_BACKEND_ASSEMBLER_DEINIT;
  } AssemblerDeinit;
};


}  // namespace backend
}  // namespace alloy


#endif  // ALLOY_BACKEND_TRACING_H_
