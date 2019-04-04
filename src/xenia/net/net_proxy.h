/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_NET_NET_PROXY_H_
#define XENIA_NET_NET_PROXY_H_

#include <cstdint>

namespace xe {
namespace net {

struct proxypacket {
  enum Type {
    kTypeInvalid = 0,
    kTypeData,

    kTypeGetDeviceList,
    kTypeDeviceList,
    kTypeSetDevice,
  };
  uint8_t type = 0;
};

struct proxypacket_data : public proxypacket {
  static const uint32_t kHeaderSize = 16; // Size - data
  uint8_t enet_dest[0x6];
  uint8_t enet_src[0x6];

  uint16_t data_len;
  uint8_t data[1500];
};

struct proxypacket_devicelist : public proxypacket {
  struct device {

  };
};

}
}

#endif // XENIA_NET_NET_PROXY_H_