/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_SOCKET_H_
#define XENIA_BASE_SOCKET_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "xenia/base/threading.h"

namespace xe {

// An asynchronous bidirectional socket.
// Sockets are wait-based and operate in a thread-free manner. Data may be
// read from or written to a socket from any thread with the wait handle as
// a mechanism to efficiently wait until new data is available.
// Consumers should wait on the wait handle and when signalled poll to see
// if the socket is still connected or new data is available. When reading
// data Receive should be called until it returns 0 new bytes and then the
// wait handle should be waited on until new data arrives.
class Socket {
 public:
  // Synchronously connects to the given hostname:port.
  static std::unique_ptr<Socket> Connect(std::string hostname, uint16_t port);

  virtual ~Socket() = default;

  // TODO(benvanik): socket info? remote IP? etc?

  // Returns a wait handle that can be used to wait on incoming client data or
  // status updates. When this handle is signalled consumers should poll for
  // new data and react accordingly.
  virtual xe::threading::WaitHandle* wait_handle() = 0;

  // Returns true if the client is connected and can send/receive data.
  virtual bool is_connected() = 0;

  // Closes the socket.
  // This will signal the wait handle.
  virtual void Close() = 0;

  // Receives incoming data up to the given capacity and returns the total
  // number of bytes received.
  // 0 is returned if there is no data available. -1 is returned if the client
  // has been closed.
  virtual size_t Receive(void* buffer, size_t buffer_capacity) = 0;

  // Asynchronously sends a set of buffers.
  // The buffers are copied and may be modified immediately after the function
  // returns.
  // Returns false if the socket is disconnected or the data cannot be sent.
  virtual bool Send(const std::pair<const void*, size_t>* buffers,
                    size_t buffer_count) = 0;

  // Asynchronously sends a set of buffers.
  // The buffers are copied and may be modified immediately after the function
  // returns.
  // Returns false if the socket is disconnected or the data cannot be sent.
  bool Send(std::vector<std::pair<const void*, size_t>> buffers) {
    return Send(buffers.data(), buffers.size());
  }

  // Asynchronously sends a buffer of data.
  // The buffer is copied and may be modified immediately after the function
  // returns.
  // Returns false if the socket is disconnected or the data cannot be sent.
  bool Send(const void* buffer, size_t buffer_length) {
    auto buffer_list = std::make_pair(buffer, buffer_length);
    return Send(&buffer_list, 1);
  }

  // Asynchronously sends a buffer of data.
  // The buffer is copied and may be modified immediately after the function
  // returns.
  // Returns false if the socket is disconnected or the data cannot be sent.
  bool Send(const std::vector<uint8_t>& buffer) {
    auto buffer_list = std::make_pair(buffer.data(), buffer.size());
    return Send(&buffer_list, 1);
  }

  // Asynchronously sends a string buffer.
  // Returns false if the socket is disconnected or the data cannot be sent.
  bool Send(const std::string& value) {
    return Send(value.data(), value.size());
  }
};

// Runs a socket server on the specified local port.
// The server accepts clients from a background thread until closed (by
// deletion). Clients are not tied to their creating server and the server
// may be closed while clients remain connected.
class SocketServer {
 public:
  // Creates a new socket server bound to the given local port.
  // The accept callback will be called from a random thread when a new client
  // connects.
  // Returns null if the server cannot be bound to the given port (in use, etc).
  static std::unique_ptr<SocketServer> Create(
      uint16_t port,
      std::function<void(std::unique_ptr<Socket> client)> accept_callback);

  virtual ~SocketServer() = default;
};

}  // namespace xe

#endif  // XENIA_BASE_SOCKET_H_
