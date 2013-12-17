/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_PROTOCOL_H_
#define XENIA_DEBUG_PROTOCOL_H_

#include <xenia/common.h>
#include <xenia/core.h>


XEDECLARECLASS2(xe, debug, DebugServer);


namespace xe {
namespace debug {


class Protocol {
public:
  Protocol(DebugServer* debug_server);
  virtual ~Protocol();

  virtual int Setup() = 0;
  virtual int WaitForClient() = 0;

protected:
  DebugServer* debug_server_;
};


}  // namespace debug
}  // namespace xe


#endif  // XENIA_DEBUG_PROTOCOL_H_
