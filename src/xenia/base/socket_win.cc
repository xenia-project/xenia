/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/socket.h"

#include <mutex>
#include <thread>

#include "xenia/base/logging.h"
#include "xenia/base/platform_win.h"

// winsock includes must come after platform_win.h:
#include <mstcpip.h>   // NOLINT(build/include_order)
#include <winsock2.h>  // NOLINT(build/include_order)
#include <ws2tcpip.h>  // NOLINT(build/include_order)

namespace xe {

void InitializeWinsock() {
  static bool has_initialized = false;
  if (!has_initialized) {
    has_initialized = true;
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2, 2), &wsa_data);
  }
}

class Win32Socket : public Socket {
 public:
  Win32Socket() = default;
  ~Win32Socket() override { Close(); }

  bool Connect(std::string& hostname, uint16_t port) {
    addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    // Resolve the server address and port.
    // May return multiple results, so attempt to connect to an address until
    // one succeeds.
    addrinfo* result = nullptr;
    auto port_string = std::to_string(port);
    int ret =
        getaddrinfo(hostname.c_str(), port_string.c_str(), &hints, &result);
    if (ret != 0) {
      XELOGE("getaddrinfo failed with error: {}", ret);
      return false;
    }
    SOCKET try_socket = INVALID_SOCKET;
    for (addrinfo* ptr = result; ptr; ptr = ptr->ai_next) {
      // Create a SOCKET for connecting to server.
      try_socket = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
      if (try_socket == INVALID_SOCKET) {
        XELOGE("socket failed with error: {}", WSAGetLastError());
        freeaddrinfo(result);
        return false;
      }
      // Connect to server.
      ret =
          connect(try_socket, ptr->ai_addr, static_cast<int>(ptr->ai_addrlen));
      if (ret == SOCKET_ERROR) {
        closesocket(try_socket);
        try_socket = INVALID_SOCKET;
        continue;
      }
      break;
    }
    freeaddrinfo(result);
    if (try_socket == INVALID_SOCKET) {
      XELOGE("Unable to connect to server");
      return false;
    }

    // Piggyback to setup the socket.
    return Accept(try_socket);
  }

  bool Accept(SOCKET socket) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    socket_ = socket;

    // Create event and bind to the socket, waiting for read/close
    // notifications.
    // Set true to start so we'll force a query of the socket on first run.
    event_ = xe::threading::Event::CreateManualResetEvent(true);
    assert_not_null(event_);
    WSAEventSelect(socket_, event_->native_handle(), FD_READ | FD_CLOSE);

    // Keepalive for a looong time, as we may be paused by the debugger/etc.
    struct tcp_keepalive alive;
    alive.onoff = TRUE;
    alive.keepalivetime = 7200000;
    alive.keepaliveinterval = 6000;
    DWORD bytes_returned;
    WSAIoctl(socket_, SIO_KEEPALIVE_VALS, &alive, sizeof(alive), nullptr, 0,
             &bytes_returned, nullptr, nullptr);

    return true;
  }

  xe::threading::WaitHandle* wait_handle() override { return event_.get(); }

  bool is_connected() override {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return socket_ != INVALID_SOCKET;
  }

  void Close() override {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (socket_ != INVALID_SOCKET) {
      SOCKET socket = socket_;
      socket_ = INVALID_SOCKET;
      shutdown(socket, SD_SEND);
      closesocket(socket);
    }

    if (event_) {
      // Set event so any future waits will immediately succeed.
      event_->Set();
    }
  }

  size_t Receive(void* buffer, size_t buffer_capacity) override {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (socket_ == INVALID_SOCKET) {
      return -1;
    }
    int ret = recv(socket_, reinterpret_cast<char*>(buffer),
                   static_cast<int>(buffer_capacity), 0);
    if (ret == SOCKET_ERROR) {
      int e = WSAGetLastError();
      if (e == WSAEWOULDBLOCK) {
        // Ok - no more data to read.
        // Reset our event so we'll block next time.
        event_->Reset();
        return 0;
      }
      XELOGE("Socket send error: {}", e);
      Close();
      return -1;
    } else if (ret == 0) {
      // Socket gracefully closed.
      Close();
      return -1;
    }
    return ret;
  }

  bool Send(const std::pair<const void*, size_t>* buffers,
            size_t buffer_count) override {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (socket_ == INVALID_SOCKET) {
      return false;
    }
    std::vector<WSABUF> buffer_data_list;
    std::vector<DWORD> buffer_data_sent;
    for (size_t i = 0; i < buffer_count; ++i) {
      WSABUF buf;
      buf.len = ULONG(buffers[i].second);
      buf.buf = reinterpret_cast<char*>(const_cast<void*>(buffers[i].first));
      buffer_data_list.emplace_back(buf);
      buffer_data_sent.emplace_back(0);
    }
    int ret = WSASend(socket_, buffer_data_list.data(), DWORD(buffer_count),
                      buffer_data_sent.data(), 0, nullptr, nullptr);
    if (ret == SOCKET_ERROR) {
      int e = WSAGetLastError();
      XELOGE("Socket send error: {}", e);
      Close();
      return false;
    }
    return true;
  }

 private:
  std::recursive_mutex mutex_;
  SOCKET socket_ = INVALID_SOCKET;
  std::unique_ptr<xe::threading::Event> event_;
};

std::unique_ptr<Socket> Socket::Connect(std::string hostname, uint16_t port) {
  InitializeWinsock();

  auto socket = std::make_unique<Win32Socket>();
  if (!socket->Connect(hostname, port)) {
    return nullptr;
  }
  return std::unique_ptr<Socket>(socket.release());
}

class Win32SocketServer : public SocketServer {
 public:
  Win32SocketServer(
      std::function<void(std::unique_ptr<Socket> client)> accept_callback)
      : accept_callback_(std::move(accept_callback)) {}
  ~Win32SocketServer() override {
    if (socket_ != INVALID_SOCKET) {
      SOCKET socket = socket_;
      socket_ = INVALID_SOCKET;
      linger so_linger;
      so_linger.l_onoff = TRUE;
      so_linger.l_linger = 30;
      setsockopt(socket, SOL_SOCKET, SO_LINGER,
                 reinterpret_cast<const char*>(&so_linger), sizeof(so_linger));
      shutdown(socket, SD_SEND);
      closesocket(socket);
    }

    if (accept_thread_) {
      // Join thread, which should die because the socket is now invalid.
      xe::threading::Wait(accept_thread_.get(), true);
      accept_thread_.reset();
    }
  }

  bool Bind(uint16_t port) {
    socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_ < 1) {
      XELOGE("Unable to create listen socket");
      return false;
    }
    struct tcp_keepalive alive;
    alive.onoff = TRUE;
    alive.keepalivetime = 7200000;
    alive.keepaliveinterval = 6000;
    DWORD bytes_returned;
    WSAIoctl(socket_, SIO_KEEPALIVE_VALS, &alive, sizeof(alive), nullptr, 0,
             &bytes_returned, nullptr, nullptr);
    int opt_value = 1;
    setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt_value), sizeof(opt_value));
    opt_value = 1;
    setsockopt(socket_, IPPROTO_TCP, TCP_NODELAY,
               reinterpret_cast<const char*>(&opt_value), sizeof(opt_value));

    sockaddr_in socket_addr = {0};
    socket_addr.sin_family = AF_INET;
    socket_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socket_addr.sin_port = htons(port);
    if (bind(socket_, reinterpret_cast<sockaddr*>(&socket_addr),
             sizeof(socket_addr)) == SOCKET_ERROR) {
      int e = WSAGetLastError();
      XELOGE("Unable to bind debug socket: {}", e);
      return false;
    }
    if (listen(socket_, 5) == SOCKET_ERROR) {
      int e = WSAGetLastError();
      XELOGE("Unable to listen on accept socket {}", e);
      return false;
    }

    accept_thread_ = xe::threading::Thread::Create({}, [this, port]() {
      xe::threading::set_name(std::string("xe::SocketServer localhost:") +
                              std::to_string(port));
      while (socket_ != INVALID_SOCKET) {
        sockaddr_in6 client_addr;
        int client_count = sizeof(client_addr);
        SOCKET client_socket = accept(
            socket_, reinterpret_cast<sockaddr*>(&client_addr), &client_count);
        if (client_socket == INVALID_SOCKET) {
          continue;
        }

        auto client = std::make_unique<Win32Socket>();
        if (!client->Accept(client_socket)) {
          XELOGE("Unable to accept socket; ignoring");
          continue;
        }
        accept_callback_(std::move(client));
      }
    });
    assert_not_null(accept_thread_);

    return true;
  }

 private:
  std::function<void(std::unique_ptr<Socket> client)> accept_callback_;
  std::unique_ptr<xe::threading::Thread> accept_thread_;

  SOCKET socket_ = INVALID_SOCKET;
};

std::unique_ptr<SocketServer> SocketServer::Create(
    uint16_t port,
    std::function<void(std::unique_ptr<Socket> client)> accept_callback) {
  InitializeWinsock();

  auto socket_server =
      std::make_unique<Win32SocketServer>(std::move(accept_callback));
  if (!socket_server->Bind(port)) {
    return nullptr;
  }
  return std::unique_ptr<SocketServer>(socket_server.release());
}

}  // namespace xe
