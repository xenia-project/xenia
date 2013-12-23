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

class MessageReader;
class MessageWriter;


class GDBClient : public DebugClient {
public:
  GDBClient(DebugServer* debug_server, socket_t socket_id);
  virtual ~GDBClient();

  socket_t socket_id() const { return socket_id_; }

  virtual int Setup();
  virtual void Close();

  virtual void SendEvent(json_t* event_json) {}

private:
  static void StartCallback(void* param);

  int PerformHandshake();
  void EventThread();
  int CheckReceive();
  void DispatchMessage(MessageReader* message);
  int PumpSendQueue();
  MessageWriter* BeginSend();
  void EndSend(MessageWriter* message);
  void Send(const char* format, ...);

  void HandleReadRegisters(const char* str);
  void HandleWriteRegisters(const char* str);
  void HandleReadRegister(const char* str);
  void HandleWriteRegister(const char* str);
  void HandleReadMemory(const char* str);
  void HandleWriteMemory(const char* str);
  void HandleAddBreakpoint(const char* str);
  void HandleRemoveBreakpoint(const char* str);

  xe_thread_ref     thread_;

  socket_t          socket_id_;
  xe_socket_loop_t* loop_;
  xe_mutex_t*       mutex_;
  std::vector<MessageReader*> message_reader_pool_;
  std::vector<MessageWriter*> message_writer_pool_;
  std::vector<MessageWriter*> pending_messages_;

  MessageReader* current_reader_;
  std::vector<MessageWriter*> send_queue_;
  bool send_queue_stalled_;
};


}  // namespace gdb
}  // namespace protocols
}  // namespace debug
}  // namespace xe


#endif  // XENIA_DEBUG_PROTOCOLS_GDB_GDB_CLIENT_H_
