/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_PROTOCOLS_WS_WS_CLIENT_H_
#define XENIA_DEBUG_PROTOCOLS_WS_WS_CLIENT_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <vector>

#include <xenia/debug/debug_client.h>


struct wslay_event_msg;
struct json_t;


namespace xe {
namespace debug {
namespace protocols {
namespace ws {


class WSClient : public DebugClient {
public:
  WSClient(DebugServer* debug_server, socket_t socket_id);
  virtual ~WSClient();

  socket_t socket_id() const { return socket_id_; }

  virtual int Setup();
  virtual void Close();

  virtual void SendEvent(json_t* event_json);

  void Write(char* value);
  void Write(const uint8_t** buffers, size_t* lengths, size_t count,
             bool binary = true);

  void OnMessage(const uint8_t* data, size_t length);
  json_t* HandleMessage(const char* command, json_t* request, bool& succeeded);

private:
  static void StartCallback(void* param);

  int PerformHandshake();
  int WriteResponse(std::string& response);
  void EventThread();

  xe_thread_ref     thread_;

  socket_t          socket_id_;
  xe_socket_loop_t* loop_;
  xe_mutex_t*       mutex_;
  std::vector<struct wslay_event_msg> pending_messages_;
};


}  // namespace ws
}  // namespace protocols
}  // namespace debug
}  // namespace xe


#endif  // XENIA_DEBUG_PROTOCOLS_WS_WS_CLIENT_H_
