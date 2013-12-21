/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_DEBUG_SERVER_H_
#define XENIA_DEBUG_DEBUG_SERVER_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <vector>


XEDECLARECLASS1(xe, Emulator);
XEDECLARECLASS2(xe, debug, DebugClient);
XEDECLARECLASS2(xe, debug, Protocol);


namespace xe {
namespace debug {


class DebugServer {
public:
  DebugServer(Emulator* emulator);
  virtual ~DebugServer();

  Emulator* emulator() const { return emulator_; }

  int Startup();
  int BeforeEntry();
  void Shutdown();

private:
  void AddClient(DebugClient* debug_client);
  void RemoveClient(DebugClient* debug_client);

  friend class DebugClient;

private:
  Emulator* emulator_;
  std::vector<Protocol*> protocols_;

  xe_mutex_t* lock_;
  std::vector<DebugClient*> clients_;
};


}  // namespace debug
}  // namespace xe


#endif  // XENIA_DEBUG_DEBUG_SERVER_H_
