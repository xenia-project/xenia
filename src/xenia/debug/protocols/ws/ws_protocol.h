/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_PROTOCOLS_WS_WS_PROTOCOL_H_
#define XENIA_DEBUG_PROTOCOLS_WS_WS_PROTOCOL_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/debug/protocol.h>


XEDECLARECLASS2(xe, debug, DebugServer);


namespace xe {
namespace debug {
namespace protocols {
namespace ws {


class WSProtocol : public Protocol {
public:
  WSProtocol(DebugServer* debug_server);
  virtual ~WSProtocol();

  virtual int Setup();

private:
  static void StartCallback(void* param);
  void AcceptThread();

protected:
  uint32_t port_;

  socket_t socket_id_;

  xe_thread_ref thread_;
  bool          running_;
};


}  // namespace ws
}  // namespace protocols
}  // namespace debug
}  // namespace xe


#endif  // XENIA_DEBUG_PROTOCOLS_WS_WS_PROTOCOL_H_
