/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_DEBUG_TARGET_H_
#define XENIA_DEBUG_DEBUG_TARGET_H_

#include <xenia/common.h>
#include <xenia/core.h>
#include <xenia/debug/debug_server.h>


struct json_t;


namespace xe {
namespace debug {


class DebugTarget {
public:
  DebugTarget(DebugServer* debug_server) :
      debug_server_(debug_server) {}
  virtual ~DebugTarget() {}

  DebugServer* debug_server() const { return debug_server_; }

  virtual json_t* OnDebugRequest(
      const char* command, json_t* request, bool& succeeded) = 0;

protected:
  DebugServer* debug_server_;
};


}  // namespace debug
}  // namespace xe


#endif  // XENIA_DEBUG_DEBUG_TARGET_H_
