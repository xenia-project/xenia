/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_PROTOCOLS_GDB_GDB_CLIENT_H_
#define XENIA_DEBUG_PROTOCOLS_GDB_GDB_CLIENT_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <vector>

#include <xenia/debug/debug_client.h>


namespace xe {
namespace debug {
namespace protocols {
namespace gdb {


class GDBClient : public DebugClient {
public:
  GDBClient(DebugServer* debug_server, int socket_id);
  virtual ~GDBClient();

  socket_t socket_id();

  virtual int Setup();

  void Write(uint8_t** buffers, size_t* lengths, size_t count);

private:
  static void StartCallback(void* param);

  int PerformHandshake();
  void EventThread();

  xe_thread_ref     thread_;

  socket_t          socket_id_;
  xe_socket_loop_t* loop_;
  xe_mutex_t*       mutex_;
};


}  // namespace gdb
}  // namespace protocols
}  // namespace debug
}  // namespace xe


#endif  // XENIA_DEBUG_PROTOCOLS_GDB_GDB_CLIENT_H_
