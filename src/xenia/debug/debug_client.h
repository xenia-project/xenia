/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_DEBUG_CLIENT_H_
#define XENIA_DEBUG_DEBUG_CLIENT_H_

#include <xenia/common.h>
#include <xenia/core.h>


XEDECLARECLASS2(xe, debug, DebugServer);


namespace xe {
namespace debug {


class DebugClient {
public:
  DebugClient(DebugServer* debug_server);
  virtual ~DebugClient();

  virtual int Setup() = 0;
  virtual void Close() = 0;

protected:
  DebugServer* debug_server_;
};


}  // namespace debug
}  // namespace xe


#endif  // XENIA_DEBUG_DEBUG_CLIENT_H_
