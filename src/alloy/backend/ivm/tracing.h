/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_BACKEND_IVM_TRACING_H_
#define ALLOY_BACKEND_IVM_TRACING_H_

#include <alloy/backend/tracing.h>


namespace alloy {
namespace backend {
namespace ivm {

const uint32_t ALLOY_BACKEND_IVM =
    alloy::backend::EventType::ALLOY_BACKEND_IVM;


class EventType {
public:
  enum {
    ALLOY_BACKEND_IVM_INIT              = ALLOY_BACKEND_IVM | (1),
    ALLOY_BACKEND_IVM_DEINIT            = ALLOY_BACKEND_IVM | (2),

    ALLOY_BACKEND_IVM_ASSEMBLER         = ALLOY_BACKEND_IVM | (1 << 20),
    ALLOY_BACKEND_IVM_ASSEMBLER_INIT    = ALLOY_BACKEND_IVM_ASSEMBLER | (1),
    ALLOY_BACKEND_IVM_ASSEMBLER_DEINIT  = ALLOY_BACKEND_IVM_ASSEMBLER | (2),
  };

  typedef struct Init_s {
    static const uint32_t event_type = ALLOY_BACKEND_IVM_INIT;
  } Init;
  typedef struct Deinit_s {
    static const uint32_t event_type = ALLOY_BACKEND_IVM_DEINIT;
  } Deinit;

  typedef struct AssemblerInit_s {
    static const uint32_t event_type = ALLOY_BACKEND_IVM_ASSEMBLER_INIT;
  } AssemblerInit;
  typedef struct AssemblerDeinit_s {
    static const uint32_t event_type = ALLOY_BACKEND_IVM_ASSEMBLER_DEINIT;
  } AssemblerDeinit;
};


}  // namespace ivm
}  // namespace backend
}  // namespace alloy


#endif  // ALLOY_BACKEND_IVM_TRACING_H_
