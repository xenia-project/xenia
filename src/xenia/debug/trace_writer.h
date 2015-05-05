/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_TRACE_WRITER_H_
#define XENIA_DEBUG_TRACE_WRITER_H_

namespace xe {
namespace debug {

enum TraceFlags {
  kTraceFunctionInfo = 1 << 0,
  kTraceInstructionReferences = 1 << 1,
  kTraceInstructionResults = 1 << 2,
};

class TraceWriter {
 public:
};

}  // namespace debug
}  // namespace xe

#endif  // XENIA_DEBUG_TRACE_WRITER_H_
