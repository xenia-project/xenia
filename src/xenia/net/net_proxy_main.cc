/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// Xenia network proxy
// This acts as a proxy between Xenia and XBOX 360 consoles.
// Unfortunately, the XBOX 360 sends packets that WinSock2 cannot receive.
// So, we have to listen for any packets sent from an XBOX 360, modify them,
// then redirect them into Xenia for HLE.

#include "xenia/base/main.h"

#include "xenia/base/byte_order.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/socket.h"
#include "xenia/base/string_buffer.h"
#include "xenia/base/threading.h"
#include "xenia/net/net_proxy.h"

#include <pcap/pcap.h>

// DEBUG
#include <WinSock2.h>

namespace xe {
namespace net {

// Ethernet II header
struct enet_header {
  uint8_t dest[6];
  uint8_t src[6];
  uint16_t type = 0x8;  // IPv4 = 0x0008 (LE)
};

struct ipv4_header {
  uint8_t header_len : 4;         // 0x0 0x5 (4 * 5 = 20)
  uint8_t version : 4;            // 0x0 IPv4 = 0x4
  uint8_t dscp;                   // 0x1
  xe::be<uint16_t> total_length;  // 0x2
  xe::be<uint16_t> ident;         // 0x4
  xe::be<uint16_t> flags;   // 0x6 flags and fragment offset (unused by UDP)
  uint8_t ttl;              // 0x8 Time to live (usually 128)
  uint8_t protocol;         // 0x9 UDP = 0x11
  uint16_t checksum;        // 0xA
  xe::be<uint32_t> ip_src;  // 0xC
  xe::be<uint32_t> ip_dst;  // 0x10
};

struct udp_pseudo_header {
  xe::be<uint32_t> ip_src;      // 0x0
  xe::be<uint32_t> ip_dst;      // 0x4
  uint8_t zero;                 // 0x8
  uint8_t protocol;             // 0x9
  xe::be<uint16_t> udp_length;  // 0xA
};

struct udp_header {
  xe::be<uint16_t> port_src;  // 0x0
  xe::be<uint16_t> port_dst;  // 0x2
  xe::be<uint16_t> length;    // 0x4
  uint16_t checksum;          // 0x6
};

std::vector<std::unique_ptr<Socket>> clients;
std::mutex clients_mutex;
threading::Fence clients_fence;

pcap_t* device_handle = nullptr;
uint8_t device_mac[6] = {0xC8, 0x60, 0x00, 0x9F, 0x0A, 0xDD};
static const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void packet_callback(uint8_t* param, const pcap_pkthdr* header,
                     const uint8_t* data) {
  auto enet = (enet_header*)data;
  if (enet->type != 0x8) {
    // Not IPv4.
    return;
  }

  auto ipv4 = (ipv4_header*)(data + sizeof(enet_header));
  if (ipv4->protocol != 0x11) {
    // Not UDP.
    return;
  }

  if (!std::memcmp(enet->src, device_mac, 6)) {
    // Drop if it's from this computer
    return;
  }

  // Check if it's a broadcast/destined for this computer
  if (std::memcmp(enet->dest, broadcast_mac, 6) &&
      std::memcmp(enet->dest, device_mac, 6)) {
    return;
  }

  proxypacket_data dp;
  std::memset(&dp, 0, sizeof(dp));
  dp.type = proxypacket::kTypeData;
  std::memcpy(dp.enet_dest, enet->dest, 6);
  std::memcpy(dp.enet_src, enet->src, 6);
  std::memcpy(dp.data, data + 0x2A,
              (size_t)std::min(1500u, header->len - 0x2A));
  dp.data_len = (uint16_t)std::min(1500u, header->len - 0x2A);

  // Send the packet over to Xenia.
  std::lock_guard<std::mutex> lock(clients_mutex);
  for (auto it = clients.begin(); it != clients.end(); it++) {
    (*it)->Send(&dp, proxypacket_data::kHeaderSize + header->len - 0x2A);
  }
}

uint16_t calculate_checksum(const uint8_t** buffers, uint16_t* buffer_len,
                            size_t num_buffers) {
  uint64_t sum = 0;
  for (size_t i = 0; i < num_buffers; i++) {
    uint16_t* data16 = (uint16_t*)buffers[i];
    uint16_t data16len = buffer_len[i] & ~1;

    for (uint16_t j = 0; j < data16len; j += 2) {
      sum += data16[j / 2];
    }

    if (buffer_len[i] & 1) {
      // Handle any stragglers.
      sum += buffers[i][buffer_len[i] - 1];
    }
  }

  while (sum > 0xFFFF) {
    // Carry
    sum = (sum & 0xFFFF) + (sum >> 16);
  }

  return (uint16_t)~sum;
}

uint16_t calculate_checksum(const uint8_t* buffer, uint16_t buffer_len) {
  return calculate_checksum(&buffer, &buffer_len, 1);
}

int send_packet(uint8_t* enet_src, uint8_t* enet_dest, const uint8_t* data,
                uint16_t data_len) {
  std::vector<uint8_t> packet_mem;
  packet_mem.reserve(data_len + 0x100);

  // Assuming UDP for now. If it's TCP we're fucked.
  auto enet = (enet_header*)packet_mem.data();
  std::memcpy(enet->src, enet_src, 6);
  std::memcpy(enet->dest, enet_dest, 6);
  enet->type = 0x8; // IPv4

  // Check if it's a broadcast packet.
  bool broadcast_packet = false;
  static const uint8_t enet_broadcast[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  if (!std::memcmp(enet_dest, enet_broadcast, 6)) {
    broadcast_packet = true;
  }

  auto ipv4 = (ipv4_header*)(packet_mem.data() + sizeof(enet_header));
  ipv4->version = 4;
  ipv4->header_len = 5;
  ipv4->dscp = 0;
  ipv4->total_length = data_len + sizeof(ipv4_header) + sizeof(udp_header);
  ipv4->ident = 'XE';  // Not too important.
  ipv4->flags = 0;
  ipv4->ttl = 64;
  ipv4->protocol = 0x11;  // IPPROTO_UDP
  ipv4->checksum = 0;
  ipv4->ip_src = 0x00000001;  // 0.0.0.1 (required for 360 packets)
  ipv4->ip_dst = broadcast_packet ? 0xFFFFFFFF   // 255.255.255.255
                                  : 0x00000001;  // 0.0.0.1
  ipv4->checksum = calculate_checksum((uint8_t*)ipv4, sizeof(ipv4_header));

  auto udp = (udp_header*)(packet_mem.data() + sizeof(enet_header) +
                           sizeof(ipv4_header));
  udp->port_src = 3074;
  udp->port_dst = 3074;
  udp->length = data_len + sizeof(udp_header);
  udp->checksum = 0;

  // UDP uses a pseudo-header for its checksum.
  // Calculating the checksum is optional for UDP!
  udp_pseudo_header pseudo_header;
  pseudo_header.ip_src = ipv4->ip_src;
  pseudo_header.ip_dst = ipv4->ip_dst;
  pseudo_header.zero = 0;
  pseudo_header.protocol = ipv4->protocol;
  pseudo_header.udp_length = udp->length;

  const uint8_t* buffers[] = {(uint8_t*)&pseudo_header, (uint8_t*)udp, data};
  uint16_t lengths[] = {sizeof(udp_pseudo_header), sizeof(udp_header),
                        data_len};
  udp->checksum = calculate_checksum(buffers, lengths, xe::countof(buffers));

  // Copy in user data.
  auto pkt_data = packet_mem.data() + sizeof(enet_header) +
                  sizeof(ipv4_header) + sizeof(udp_header);
  std::memcpy(pkt_data, data, data_len);

  // Now send the packet
  // TODO: Linux can just use a raw socket here.
  pcap_sendpacket(device_handle, packet_mem.data(),
                  sizeof(enet_header) + sizeof(ipv4_header) +
                      sizeof(udp_header) + data_len);

  return 0;
}

int net_proxy_client_thread() {
  while (true) {
    // Read from the client sockets.
    if (clients.size() == 0) {
      clients_fence.Wait();
    }

    std::vector<threading::WaitHandle*> wait_handles;
    for (int i = 0; i < clients.size(); i++) {
      wait_handles.push_back(clients[i]->wait_handle());
    }

    auto wr = threading::WaitMultiple(wait_handles.data(), wait_handles.size(),
                                      false, false);
    if (wr.first != threading::WaitResult::kSuccess) {
      continue;
    }

    uint8_t buf[0x800];
    for (size_t i = wr.second; i < clients.size(); i++) {
      size_t r = clients[i]->Receive(buf, 0x800);
      if (r == -1) {
        // Dead client.
        std::lock_guard<std::mutex> lock(clients_mutex);
        clients.erase(clients.begin() + i);
        i--;

        continue;
      } else if (r == 0) {
        // No data.
        continue;
      }

      auto pkt = (proxypacket*)buf;
      if (pkt->type == proxypacket::kTypeData) {
        // Send data on Xenia's behalf
        auto pkt_data = (proxypacket_data*)pkt;

        send_packet(device_mac, pkt_data->enet_dest, pkt_data->data,
                    pkt_data->data_len);
      } else if (pkt->type == proxypacket::kTypeGetDeviceList) {
        // Send a device list over to Xenia.
        pcap_if_t* all_devices;
        char error_buffer[PCAP_ERRBUF_SIZE];

        int res = pcap_findalldevs(&all_devices, error_buffer);
        if (res < 0) {
          XELOGE("Could not generate device list: pcap_findalldevs failed (%s)",
                 error_buffer);
          continue;
        }

        // TODO: Need a utility class that can stream into a buffer (or
        // whatever)

        // Free the device list.
        pcap_freealldevs(all_devices);
      }
    }
  }
}

int net_proxy_main(const std::vector<std::wstring>& args) {
  pcap_if_t* all_devices;
  char error_buffer[PCAP_ERRBUF_SIZE];

  int res = pcap_findalldevs(&all_devices, error_buffer);
  if (res < 0) {
    XELOGE("pcap_findalldevs failed!");
    return 1;
  }

  // DEBUG: blah use device 2
  device_handle =
      pcap_open_live(all_devices->next->name, 65536, 1, 1000, error_buffer);
  if (device_handle == NULL) {
    return 1;
  }

  StringBuffer filter;
  filter.Append(
      "(udp and ((ip host 0.0.0.1) or (dst portrange 3074-3075) or (dst "
      "portrange 14000-14001)))");

  bpf_program filter_code;
  if (pcap_compile(device_handle, &filter_code, filter.GetString(), 1,
                   0xFFFFFFFF)) {
    return 1;
  }

  if (pcap_setfilter(device_handle, &filter_code) < 0) {
    return 1;
  }

  XELOGD("Listening on %s", all_devices->next->description);

  // Create the socket server.
  auto server = SocketServer::Create(45981, [](std::unique_ptr<Socket> client) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    clients.push_back(std::move(client));
    clients_fence.Signal();
  });

  // We no longer need this buffer.
  pcap_freealldevs(all_devices);

  // Create a server thread
  threading::Thread::CreationParameters params;
  params.create_suspended = false;
  params.initial_priority = 0;
  params.stack_size = 1024 * 1024;
  auto thread = threading::Thread::Create(params, net_proxy_client_thread);

  pcap_loop(device_handle, 0, packet_callback, NULL);

  pcap_close(device_handle);
  return 0;
}

}  // namespace net
}  // namespace xe

DEFINE_ENTRY_POINT(L"xenia-net-proxy", L"xenia-net-proxy",
                   xe::net::net_proxy_main);