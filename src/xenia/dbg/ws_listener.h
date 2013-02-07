/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DBG_WS_LISTENER_H_
#define XENIA_DBG_WS_LISTENER_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/dbg/listener.h>


namespace xe {
namespace dbg {


class Debugger;


class WsListener : public Listener {
public:
  WsListener(Debugger* debugger, xe_pal_ref pal, uint32_t port);
  virtual ~WsListener();

  virtual int Setup();
  virtual int WaitForClient();

protected:
  uint32_t port_;

  socket_t socket_id_;
};


}  // namespace dbg
}  // namespace xe


#endif  // XENIA_DBG_WS_LISTENER_H_
