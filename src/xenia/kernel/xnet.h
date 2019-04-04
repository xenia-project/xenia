/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XNET_H_
#define XENIA_KERNEL_XNET_H_

#include <cstdint>

#include "xenia/xbox.h"
#include "xenia/base/socket.h"
#include "xenia/kernel/xsocket.h"
#include "xenia/kernel/xthread.h"

namespace xe {
namespace kernel {

class KernelState;

struct XNKEY {
  uint8_t key[0x10];
};

class XNet {
 public:
  XNet(KernelState* kernel_state);
  X_STATUS Initialize();

  int SendPacket(XSocket* socket, N_XSOCKADDR_IN* to, const uint8_t* buf,
                 size_t buf_len);

  // Validates the packet's HMAC-SHA1.
  bool ValidatePacket(const uint8_t* packet, size_t packet_len,
                      const uint8_t* packet_hmac);
  void GenerateLANKeys(uint8_t* lan_key);
  uint64_t BuildFeed(uint32_t* key, uint32_t seed);
  void CryptBuffer(uint64_t* key, size_t key_length, uint64_t* buffer,
                   size_t buffer_length, uint64_t feed, bool encrypt);

  KernelState* kernel_state() const { return kernel_state_; }

 private:
  int ProxyRecvThreadEntry();
  int RecvPacket(uint8_t* enet_from, uint8_t* enet_to, uint8_t* data,
                 uint16_t data_len);

  KernelState* kernel_state_ = nullptr;

  uint8_t key_hmac_[0x10];    // 0x1504 XeKeysHmacSha
  uint64_t key_des3_[3];      // 0x1514
  uint32_t key_cbc_feed_[2];  // 0x152C

  std::unique_ptr<XHostThread> proxy_recv_thread_;
  std::unique_ptr<Socket> proxy_socket_;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XNET_H_