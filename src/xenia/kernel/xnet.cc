/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xnet.h"

// DEBUG
#include "xenia/cpu/processor.h"
#include "xenia/cpu/raw_module.h"

#include "xenia/base/logging.h"
#include "xenia/base/socket.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/user_module.h"
#include "xenia/kernel/xsocket.h"
#include "xenia/kernel/xthread.h"
#include "xenia/net/net_proxy.h"

#include "third_party/crypto/TinySHA1.hpp"
#include "third_party/crypto/des/des3.h"

#include <thread>

#ifdef XE_PLATFORM_WIN32
// clang-format off
#include "xenia/base/platform_win.h"
#include <WS2tcpip.h>
#include <WinSock2.h>
// clang-format on
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#endif

namespace xe {
namespace kernel {

// For now, this'll be here.
class HMACSHA1 {
 public:
  HMACSHA1(const uint8_t* key, uint32_t key_len) { reset(key, key_len); }
  void reset(const uint8_t* key, uint32_t key_len) {
    uint8_t k_ipad[0x40];
    uint32_t temp_key[5];

    // Hash the key if it's > block size
    if (key_len > 0x40) {
      sha1::SHA1 key_sha;
      key_sha.processBytes(key, key_len);

      key_sha.finalize(temp_key);
      key = (uint8_t*)&temp_key;
      key_len = 0x14;
    }

    uint32_t i;
    for (i = 0; i < key_len; i++) {
      k_ipad[i] = key[i] ^ 0x36;
      k_opad_[i] = key[i] ^ 0x5C;
    }

    // Right-padding
    for (; i < 0x40; i++) {
      k_ipad[i] = 0x36;
      k_opad_[i] = 0x5C;
    }

    sha_.reset();
    sha_.processBytes(k_ipad, 0x40);
  }

  void processBytes(const void* data, size_t len) {
    sha_.processBytes(data, len);
  }

  void result(uint8_t* digest) {
    sha_.finalize(digest);
    sha_.reset();

    sha_.processBytes(k_opad_, 0x40);
    sha_.processBytes(digest, 0x14);
    sha_.finalize(digest);
  }

  void result(uint32_t* digest) {
    sha_.finalize(digest);
    sha_.reset();

    sha_.processBytes(k_opad_, 0x40);
    sha_.processBytes(digest, 0x14);
    sha_.finalize(digest);
  }

 private:
  sha1::SHA1 sha_;
  uint8_t k_opad_[0x40];
};

// Retail key 0x19 from the keyvault (ROAMABLE_OBFUSCATION_KEY)
// http://www.free60.org/wiki/Profile:Account
static const uint8_t roamable_obfuscation_key[] = {
    0xE1, 0xBC, 0x15, 0x9C, 0x73, 0xB1, 0xEA, 0xE9,
    0xAB, 0x31, 0x70, 0xF3, 0xAD, 0x47, 0xEB, 0xF3,
};

// Packet data, game data encrypted from byte 0x4->gap
static const uint8_t data[] = {
    // Flags(?) (4 bytes)
    0x29, 0x00, 0x00, 0x00,

    // Encrypted game data
    0x79, 0xF6, 0x8E, 0xD8, 0xBF, 0xEB, 0x2A, 0xE4, 0x3A, 0xF3, 0xB3, 0xD8,
    0xE4, 0x8F, 0xE1, 0x92, 0x39, 0x30, 0x3D, 0x10, 0x9C, 0x00, 0x1A, 0xD5,

    // ports (first byte encrypted above)
    0x96, 0x69, 0x96,

    // Title ID
    0x30, 0x08, 0x41, 0x45,
    // Title Version
    0x01, 0x07, 0x00, 0x00,

    // XamGetSystemVersion
    0x00, 0x51, 0x44, 0x20,

    // # bytes encrypted div 8
    0x03,

    // RND key
    0xD1, 0xBC,

    // HMAC-SHA1
    // Behind this is (when generated):
    // [RND key] [first 4 bytes]
    0x4B, 0x86, 0xE3, 0x28, 0x7E, 0x5A, 0x85, 0xE7, 0x3B, 0x20,
};

static const uint8_t game_data[] = {
    0x00, 0x15, 0xFF, 0xFF, 0xFF, 0xFF, 0x30, 0x08, 0x41, 0x45, 0x01, 'X',
    'E',  'N',  'I',  'A',  ' ',  ' ',  ' ',  0x00, 0x00, 0x00, 0x00,
};

#pragma pack(push, 1)
// 29 bytes
struct packet_footer {
  xe::be<uint16_t> port_from;  // 0x0
  xe::be<uint16_t> port_to;    // 0x2
  uint32_t title_id;           // 0x4 0x0
  uint32_t title_version;      // 0x8 0x4

  uint32_t system_version;        // 0xC 0x8 XamGetSystemVersion
  uint8_t bytes_encrypted_div_8;  // 0x10 0xC
  uint16_t feed_seed;             // 0x11 0xD

  uint8_t hmac[0xA];  // 0x13
};
#pragma pack(pop)

XNet::XNet(KernelState* kernel_state) : kernel_state_(kernel_state) {}

X_STATUS XNet::Initialize() {
  auto xex = kernel_state_->GetExecutableModule()->xex_module();

  // TODO: support cross-platform system link
  uint32_t system_flags = 0;
  xex->GetOptHeader(XEX_HEADER_SYSTEM_FLAGS, &system_flags);
  assert_zero(system_flags & XEX_SYSTEM_CROSS_PLATFORM_SYSTEM_LINK);

  // Generate some keys
  xex2_opt_lan_key* lan_key_hdr = nullptr;
  xex->GetOptHeader(XEX_HEADER_LAN_KEY, &lan_key_hdr);
  GenerateLANKeys(lan_key_hdr->key);

  // Now listen for packets from our little utility
  proxy_socket_ = Socket::Connect("127.0.0.1", 45981);
  if (!proxy_socket_) {
    xe::FatalError(__FILE__ ": DEBUG - Could not connect to network proxy!");
  }

  proxy_recv_thread_ = std::make_unique<XHostThread>(
      kernel_state_, 0, 0, std::bind(&XNet::ProxyRecvThreadEntry, this));
  proxy_recv_thread_->Create();

  // Test
  // SendPacket(nullptr, nullptr, game_data, sizeof(game_data));

  return X_STATUS_SUCCESS;
}

int XNet::ProxyRecvThreadEntry() {
  uint8_t buf[0x600];
  while (true) {
    xe::threading::Wait(proxy_socket_->wait_handle(), false);
    size_t res = proxy_socket_->Receive(buf, 0x600);
    if (res == -1) {
      // Server closed.
      break;
    }

    auto proxy_packet = (net::proxypacket*)buf;
    if (proxy_packet->type == net::proxypacket::kTypeData) {
      auto packet = (net::proxypacket_data*)buf;

      RecvPacket(packet->enet_src, packet->enet_dest, packet->data,
                 packet->data_len);
    }
  }

  return 0;
}

int XNet::SendPacket(XSocket* socket, N_XSOCKADDR_IN* to, const uint8_t* buf,
                     size_t buf_len) {
  auto data = net::proxypacket_data();
  data.type = net::proxypacket::kTypeData;

  std::memset(data.enet_src, 0, 6);      // Ignored.
  std::memset(data.enet_dest, 0xFF, 6);  // broadcast (hardcoded for now)

  size_t rounded_buf_len = xe::round_up(buf_len, 8);
  std::vector<uint8_t> xnet_packet_data;
  xnet_packet_data.resize(0x4 + rounded_buf_len + sizeof(packet_footer));

  uint8_t* xnet_packet = xnet_packet_data.data();
  auto footer = (packet_footer*)(xnet_packet + 0x4 + buf_len);

  size_t extra_bytes = rounded_buf_len - buf_len;
  uint8_t padding_bytes = 0;
  if (extra_bytes > 4) {
    // Need to pad it.
    padding_bytes = uint8_t(extra_bytes - 4);

    footer = (packet_footer*)(xnet_packet + 0x4 + buf_len + padding_bytes);
  }

  std::memset(footer, 0, sizeof(packet_footer));
  std::memcpy(xnet_packet + 0x4, buf, buf_len);  // Game data

  // DEBUG
  // footer->port_from = socket ? socket->bound_port() : 27030;
  // footer->port_to = to ? to->sin_port : 27030;

  // Encrypt the packet
  // Calculate how many bytes we need to encrypt. It's okay for us to encrypt
  // part of the footer.
  size_t n_bytes_encrypted = xe::round_up(buf_len, 8);
  footer->bytes_encrypted_div_8 = (uint8_t)(n_bytes_encrypted / 8);
  size_t footer_enc_bytes = n_bytes_encrypted - buf_len;
  size_t footer_unenc_bytes =
      sizeof(packet_footer) - std::min(extra_bytes, size_t(4));

  // Values
  // 0xE9 - footer shifted >> 3 bytes (excl. 4 byte ports)
  // 0xA9 - footer shifted >> 1 byte (excl. 4 byte ports)
  // 0x29 - footer not shifted
  //
  // Bitfield/flags:
  // 0x29
  //     001        0        1010
  //  [padding]   [unk]  [block size(?)]
  //
  // 0x08 - unk
  // 0x01 - unk
  uint8_t bf = 0x08 | 0x01;
  bf |= (extra_bytes & 7) << 5;
  xnet_packet[0] = bf;

  xex2_opt_execution_info* exec_info = nullptr;
  kernel_state()->GetExecutableModule()->GetOptHeader(XEX_HEADER_EXECUTION_INFO,
                                                      &exec_info);

  footer->title_id = exec_info->title_id;
  footer->title_version = (uint32_t)exec_info->version.value.value;
  footer->system_version = 0x20445100;  // HARDCODED

  // TODO: This should be random.
  uint32_t feed_seed = 0x06660666;
  footer->feed_seed = feed_seed & 0xFFFF;

  uint64_t feed = BuildFeed(key_cbc_feed_, feed_seed);

  // Now encrypt w/ DES3
  CryptBuffer(key_des3_, 0x18, (uint64_t*)(xnet_packet + 0x4),
              n_bytes_encrypted, feed, true);

  // Generate the HMAC
  xe::store(footer->hmac, footer->feed_seed);
  std::memcpy(footer->hmac + 0x2, xnet_packet, 4);  // Copy the first byte

  HMACSHA1 hmac(key_hmac_, 0x10);

  // Step 1: Unencrypted bytes
  hmac.processBytes(xnet_packet + 0x4 + n_bytes_encrypted,
                    footer_unenc_bytes - 4);

  // Step 2: Encrypted bytes
  hmac.processBytes(xnet_packet + 0x4, n_bytes_encrypted);

  uint8_t digest[0x14];
  hmac.result(digest);

  std::memcpy(footer->hmac, digest, 0xA);

  // Copy the XNet packet into our proxy buffer
  std::memcpy(data.data, xnet_packet,
              std::min(0x4 + buf_len + padding_bytes + sizeof(packet_footer),
                       size_t(1500)));
  data.data_len =
      (uint16_t)(0x4 + buf_len + padding_bytes + sizeof(packet_footer));
  proxy_socket_->Send(&data, sizeof(data));

  return 0;
}

int XNet::RecvPacket(uint8_t* enet_from, uint8_t* enet_to, uint8_t* data,
                     uint16_t data_len) {
  auto footer = (packet_footer*)(data + (data_len - sizeof(packet_footer)));
  assert_true(footer->bytes_encrypted_div_8 * 8 < data_len);

  if (!ValidatePacket(data, data_len, footer->hmac)) {
    return -1;
  }

  // Decrypt the packet.
  uint32_t rnd_seed = footer->feed_seed;
  rnd_seed |= (rnd_seed << 16);

  uint64_t feed = BuildFeed(key_cbc_feed_, rnd_seed);
  CryptBuffer(key_des3_, 0x18, (uint64_t*)(data + 0x4),
              footer->bytes_encrypted_div_8 * 8, feed, false);

  size_t actual_data_len = footer->bytes_encrypted_div_8 * 8;

  // If there's padding (above 4 bytes), subtract it.
  uint8_t extra_bytes = (data[0] >> 5) & 7;
  uint8_t padding_bytes = 0;
  if (extra_bytes > 4) {
    padding_bytes = extra_bytes - 4;
  }

  actual_data_len -= extra_bytes;

  // Dispatch the packet.
  auto sockets = kernel_state_->object_table()->GetObjectsByType<XSocket>(
      XObject::kTypeSocket);
  for (auto sock : sockets) {
    if (sock->bound_port() == footer->port_to) {
      // Redirect the data into the socket.
      sock->QueuePacket(0x1, footer->port_from, data + 0x4, actual_data_len);
    }
  }

  return 0;
}

bool XNet::ValidatePacket(const uint8_t* packet, size_t packet_len,
                          const uint8_t* packet_hmac) {
  HMACSHA1 hmac(key_hmac_, 0x10);
  uint8_t digest[0x14];

  auto footer =
      (const packet_footer*)(packet + packet_len - sizeof(packet_footer));

  // Find out how long the unencrypted portion of the footer is
  const uint8_t* footer_start =
      packet + 0x4 + footer->bytes_encrypted_div_8 * 8;
  size_t footer_len = packet_len - 0x4 - footer->bytes_encrypted_div_8 * 8;

  // Craft the actual footer
  std::vector<uint8_t> footer_mem;
  footer_mem.resize(footer_len);

  uint8_t* footer_hmac = footer_mem.data();
  uint8_t* footer_hmac_end = footer_hmac + footer_len;
  std::memcpy(footer_hmac, footer_start, footer_len);
  std::memset(footer_hmac_end - 0xA, 0, 0xA);  // Zero the HMAC field.

  // Store in little endian.
  xe::store<uint16_t>(footer_hmac_end - 0xA, footer->feed_seed);
  std::memcpy(footer_hmac_end - 0x8, packet, 0x4);

  // First: Process the unencrypted portion of the packet (w/ a pseudo-header
  // in what will be the HMAC)
  hmac.processBytes(footer_hmac,
                    footer_len & ~0x7);  // FIXME: Is the length OK?

  // Now process the encrypted data.
  hmac.processBytes(packet + 0x4, footer->bytes_encrypted_div_8 * 8);
  hmac.result(digest);

  if (std::memcmp(digest, packet_hmac, 0xA) == 0) {
    return true;
  }

  return false;
}

void XNet::GenerateLANKeys(uint8_t* lan_key) {
  HMACSHA1 hmac(roamable_obfuscation_key, 0x10);
  uint8_t digest[3][0x14];
  uint8_t key_buffer[0x11];

  std::memcpy(key_buffer + 1, lan_key, 0x10);

  // Round 1
  // Generates the HMAC key for validation/verification
  key_buffer[0] = 0;
  hmac.processBytes(key_buffer, 0x11);
  hmac.result(digest[0]);
  hmac.reset(roamable_obfuscation_key, 0x10);

  // Round 2
  // Generates the DES3 key used for packet encryption
  key_buffer[0] = 1;
  hmac.processBytes(key_buffer, 0x11);
  hmac.result(digest[1]);
  hmac.reset(roamable_obfuscation_key, 0x10);

  // Round 3
  // Generates a key used to make the DES3 CBC initial feed
  key_buffer[0] = 2;
  hmac.processBytes(key_buffer, 0x11);
  hmac.result(digest[2]);
  hmac.reset(roamable_obfuscation_key, 0x10);

  // DES3 key
  std::memcpy((uint8_t*)key_des3_, &digest[0][0x10], 4);
  std::memcpy((uint8_t*)key_des3_ + 0x4, digest[1], 0x14);
  DES::set_parity((ui8*)key_des3_, 0x18, (ui8*)key_des3_);
  for (int i = 0; i < 3; i++) {
    // Byteswap the key for use.
    key_des3_[i] = xe::byte_swap(key_des3_[i]);
  }

  std::memcpy(key_hmac_, digest[0], 0x10);     // HMAC validation key
  std::memcpy(key_cbc_feed_, digest[2], 0x8);  // CBC key
}

uint64_t XNet::BuildFeed(uint32_t* key, uint32_t seed) {
  // Note: This code will only work on little-endian CPUs!
  uint64_t k0 = 0, k1 = 0;
  uint32_t r[2];

  k0 = uint64_t(key[0]) * uint64_t(seed);
  k1 = uint64_t(key[1]) * uint64_t(seed);

  // XOR the high result of one multiplication with the low of the other.
  r[0] = (k1 >> 32) ^ (k0 & 0xFFFFFFFF);
  r[1] = (k0 >> 32) ^ (k1 & 0xFFFFFFFF);

  // Final byteswap (result must be in big-endian)
  *(uint64_t*)r = xe::byte_swap(*(uint64_t*)r);
  return *reinterpret_cast<uint64_t*>(r);
}

void XNet::CryptBuffer(uint64_t* key, size_t key_length, uint64_t* buffer,
                       size_t buffer_length, uint64_t feed, bool encrypt) {
  // This MUST BE a multiple of the block size!
  assert_true(buffer_length % 8 == 0);

  if (key_length == 0x18) {
    // DES3 CBC
    DES3 des3(key[0], key[1], key[2]);

    uint64_t last_block = feed;
    for (size_t i = 0; i < buffer_length / 8; i++) {
      uint64_t block = xe::byte_swap(buffer[i]);
      if (encrypt) {
        last_block = des3.encrypt(block ^ last_block);
        buffer[i] = xe::byte_swap(last_block);
      } else {
        buffer[i] = xe::byte_swap(des3.decrypt(block) ^ last_block);
        last_block = block;
      }
    }
  }
}

}  // namespace kernel
}  // namespace xe