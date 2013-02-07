/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DBG_LISTENER_H_
#define XENIA_DBG_LISTENER_H_

#include <xenia/common.h>
#include <xenia/core.h>


namespace xe {
namespace dbg {


class Debugger;


class Listener {
public:
  Listener(Debugger* debugger, xe_pal_ref pal);
  virtual ~Listener();

  virtual int Setup() = 0;
  virtual int WaitForClient() = 0;

protected:
  Debugger*   debugger_;
  xe_pal_ref  pal_;
};


}  // namespace dbg
}  // namespace xe


#endif  // XENIA_DBG_LISTENER_H_
