/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_BACKEND_X64_TRACING_H_
#define ALLOY_BACKEND_X64_TRACING_H_

#include <alloy/backend/tracing.h>


namespace alloy {
namespace backend {
namespace x64 {

const uint32_t ALLOY_BACKEND_X64 =
    alloy::backend::EventType::ALLOY_BACKEND_X64;


class EventType {
public:
  enum {
    ALLOY_BACKEND_X64_INIT              = ALLOY_BACKEND_X64 | (1),
    ALLOY_BACKEND_X64_DEINIT            = ALLOY_BACKEND_X64 | (2),

    ALLOY_BACKEND_X64_ASSEMBLER         = ALLOY_BACKEND_X64 | (1 << 20),
    ALLOY_BACKEND_X64_ASSEMBLER_INIT    = ALLOY_BACKEND_X64_ASSEMBLER | (1),
    ALLOY_BACKEND_X64_ASSEMBLER_DEINIT  = ALLOY_BACKEND_X64_ASSEMBLER | (2),
  };

  typedef struct {
    static const uint32_t event_type = ALLOY_BACKEND_X64_INIT;
  } Init;
  typedef struct {
    static const uint32_t event_type = ALLOY_BACKEND_X64_DEINIT;
  } Deinit;

  typedef struct {
    static const uint32_t event_type = ALLOY_BACKEND_X64_ASSEMBLER_INIT;
  } AssemblerInit;
  typedef struct {
    static const uint32_t event_type = ALLOY_BACKEND_X64_ASSEMBLER_DEINIT;
  } AssemblerDeinit;
};


}  // namespace x64
}  // namespace backend
}  // namespace alloy


#endif  // ALLOY_BACKEND_X64_TRACING_H_
