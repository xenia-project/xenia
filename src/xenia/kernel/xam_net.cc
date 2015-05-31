/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#define _WINSOCK_DEPRECATED_NO_WARNINGS  // inet_addr
#include <winsock2.h>

#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam_private.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {

void LoadSockaddr(const uint8_t* ptr, sockaddr* out_addr) {
  out_addr->sa_family = xe::load_and_swap<uint16_t>(ptr + 0);
  switch (out_addr->sa_family) {
    case AF_INET: {
      auto in_addr = reinterpret_cast<sockaddr_in*>(out_addr);
      in_addr->sin_port = xe::load_and_swap<uint16_t>(ptr + 2);
      // Maybe? Depends on type.
      in_addr->sin_addr.S_un.S_addr = xe::load_and_swap<uint32_t>(ptr + 4);
      break;
    }
    default:
      assert_unhandled_case(out_addr->sa_family);
      break;
  }
}

void StoreSockaddr(const sockaddr& addr, uint8_t* ptr) {
  switch (addr.sa_family) {
    case AF_UNSPEC:
      std::memset(ptr, 0, sizeof(addr));
      break;
    case AF_INET: {
      auto& in_addr = reinterpret_cast<const sockaddr_in&>(addr);
      xe::store_and_swap<uint16_t>(ptr + 0, in_addr.sin_family);
      xe::store_and_swap<uint16_t>(ptr + 2, in_addr.sin_port);
      // Maybe? Depends on type.
      xe::store_and_swap<uint32_t>(ptr + 4, in_addr.sin_addr.S_un.S_addr);
      break;
    }
    default:
      assert_unhandled_case(addr.sa_family);
      break;
  }
}

//typedef struct {
//  BYTE cfgSizeOfStruct;
//  BYTE cfgFlags;
//  BYTE cfgSockMaxDgramSockets;
//  BYTE cfgSockMaxStreamSockets;
//  BYTE cfgSockDefaultRecvBufsizeInK;
//  BYTE cfgSockDefaultSendBufsizeInK;
//  BYTE cfgKeyRegMax;
//  BYTE cfgSecRegMax;
//  BYTE cfgQosDataLimitDiv4;
//  BYTE cfgQosProbeTimeoutInSeconds;
//  BYTE cfgQosProbeRetries;
//  BYTE cfgQosSrvMaxSimultaneousResponses;
//  BYTE cfgQosPairWaitTimeInSeconds;
//} XNetStartupParams;

SHIM_CALL NetDll_XNetStartup_shim(PPCContext* ppc_context,
                                  KernelState* kernel_state) {
  uint32_t arg0 = SHIM_GET_ARG_32(0);
  uint32_t params_ptr = SHIM_GET_ARG_32(1);

  XELOGD("NetDll_XNetStartup(%d, %.8X)", arg0, params_ptr);

  SHIM_SET_RETURN_32(0);
}

SHIM_CALL NetDll_XNetCleanup_shim(PPCContext* ppc_context,
                                  KernelState* kernel_state) {
  uint32_t arg0 = SHIM_GET_ARG_32(0);
  uint32_t params_ptr = SHIM_GET_ARG_32(1);

  XELOGD("NetDll_XNetCleanup(%d, %.8X)", arg0, params_ptr);

  SHIM_SET_RETURN_32(0);
}

SHIM_CALL NetDll_XNetRandom_shim(PPCContext* ppc_context,
                                 KernelState* kernel_state) {
  uint32_t arg0 = SHIM_GET_ARG_32(0);
  uint32_t buffer_ptr = SHIM_GET_ARG_32(1);
  uint32_t length = SHIM_GET_ARG_32(2);

  XELOGD("NetDll_XNetRandom(%d, %.8X, %d)", arg0, buffer_ptr, length);

  // For now, constant values.
  // This makes replicating things easier.
  std::memset(SHIM_MEM_ADDR(buffer_ptr), 0xBB, length);

  SHIM_SET_RETURN_32(0);
}

SHIM_CALL NetDll_WSAStartup_shim(PPCContext* ppc_context,
                                 KernelState* kernel_state) {
  uint32_t arg0 = SHIM_GET_ARG_32(0); // always 1?
  uint32_t version = SHIM_GET_ARG_16(1);
  uint32_t data_ptr = SHIM_GET_ARG_32(2);

  XELOGD("NetDll_WSAStartup(%d, %.4X, %.8X)", arg0, version, data_ptr);

  // TODO: Abstraction layer needed
  WSADATA wsaData;
  ZeroMemory(&wsaData, sizeof(WSADATA));
  int ret = WSAStartup(version, &wsaData);

  auto data_out = kernel_state->memory()->TranslateVirtual(data_ptr);

  if (data_ptr) {
    xe::store_and_swap<uint16_t>(data_out + 0x000, wsaData.wVersion);
    xe::store_and_swap<uint16_t>(data_out + 0x002, wsaData.wHighVersion);
    std::memcpy(data_out + 0x004, wsaData.szDescription, 0x100);
    std::memcpy(data_out + 0x105, wsaData.szSystemStatus, 0x80);
    xe::store_and_swap<uint16_t>(data_out + 0x186, wsaData.iMaxSockets);
    xe::store_and_swap<uint16_t>(data_out + 0x188, wsaData.iMaxUdpDg);

    // Some games (PoG) want this value round-tripped - they'll compare if it
    // changes and bugcheck if it does.
    uint32_t vendor_ptr = SHIM_MEM_32(data_ptr + 0x190);
    SHIM_SET_MEM_32(data_ptr + 0x190, vendor_ptr);
  }

  SHIM_SET_RETURN_32(ret);
}

SHIM_CALL NetDll_WSACleanup_shim(PPCContext* ppc_context,
                                 KernelState* kernel_state) {
  uint32_t arg0 = SHIM_GET_ARG_32(0);

  XELOGD("NetDll_WSACleanup(%d)", arg0);
  int ret = WSACleanup();

  SHIM_SET_RETURN_32(ret);
}

SHIM_CALL NetDll_WSAGetLastError_shim(PPCContext* ppc_context,
                                      KernelState* kernel_state) {
  XELOGD("NetDll_WSAGetLastError()");

  int err = WSAGetLastError();
  SHIM_SET_RETURN_32(err);
}

// typedef struct {
// 	IN_ADDR     ina;                            // IP address (zero if not static/DHCP)
// 	IN_ADDR     inaOnline;                      // Online IP address (zero if not online)
// 	WORD        wPortOnline;                    // Online port
// 	BYTE        abEnet[6];                      // Ethernet MAC address
// 	BYTE        abOnline[20];                   // Online identification
// } XNADDR;

SHIM_CALL NetDll_XNetGetTitleXnAddr_shim(PPCContext* ppc_context,
                                         KernelState* kernel_state) {
  uint32_t arg0 = SHIM_GET_ARG_32(0); // constant 1?
  uint32_t addr_ptr = SHIM_GET_ARG_32(1); // XNADDR

  XELOGD("NetDll_XNetGetTitleXnAddr(%d, %.8X)", arg0, addr_ptr);

  auto addr = kernel_state->memory()->TranslateVirtual(addr_ptr);

  xe::store<uint32_t>(addr + 0x0, htonl(INADDR_LOOPBACK));
  xe::store_and_swap<uint32_t>(addr + 0x4, 0);
  xe::store_and_swap<uint16_t>(addr + 0x8, 0);
  std::memset(addr + 0xA, 0, 6);
  std::memset(addr + 0x10, 0, 20);

  SHIM_SET_RETURN_32(0x00000004); // XNET_GET_XNADDR_STATIC
}

SHIM_CALL NetDll_XNetGetEthernetLinkStatus_shim(PPCContext* ppc_context,
                                                KernelState* kernel_state) {
  // Games seem to call this before *Startup. If we return 0, they don't even
  // try.
  uint32_t arg0 = SHIM_GET_ARG_32(0);

  XELOGD("NetDll_XNetGetEthernetLinkStatus(%d)", arg0);

  SHIM_SET_RETURN_32(0);
}

SHIM_CALL NetDll_inet_addr_shim(PPCContext* ppc_context,
                                KernelState* kernel_state) {
  uint32_t cp_ptr = SHIM_GET_ARG_32(0);

  auto cp = reinterpret_cast<const char*>(SHIM_MEM_ADDR(cp_ptr));
  XELOGD("NetDll_inet_addr(%.8X(%s))", cp_ptr, cp);
  uint32_t addr = inet_addr(cp);

  SHIM_SET_RETURN_32(addr);
}

SHIM_CALL NetDll_socket_shim(PPCContext* ppc_context,
                             KernelState* kernel_state) {
  uint32_t arg0 = SHIM_GET_ARG_32(0);
  uint32_t af = SHIM_GET_ARG_32(1);
  uint32_t type = SHIM_GET_ARG_32(2);
  uint32_t protocol = SHIM_GET_ARG_32(3);

  XELOGD("NetDll_socket(%d, %d, %d, %d)", arg0, af, type, protocol);
  if (protocol == 0xFE) {
    protocol = IPPROTO_UDP;
  }

  SOCKET socket_handle = socket(af, type, protocol);
  assert_true(socket_handle >> 32 == 0);

  SHIM_SET_RETURN_32(static_cast<uint32_t>(socket_handle));
}

SHIM_CALL NetDll_closesocket_shim(PPCContext* ppc_context,
                                  KernelState* kernel_state) {
  uint32_t arg0 = SHIM_GET_ARG_32(0);
  uint32_t socket_handle = SHIM_GET_ARG_32(1);

  XELOGD("NetDll_closesocket(%d, %.8X)", arg0, socket_handle);
  int ret = closesocket(socket_handle);

  SHIM_SET_RETURN_32(ret);
}

SHIM_CALL NetDll_setsockopt_shim(PPCContext* ppc_context,
                                 KernelState* kernel_state) {
  uint32_t arg0 = SHIM_GET_ARG_32(0);
  uint32_t socket_handle = SHIM_GET_ARG_32(1);
  uint32_t level = SHIM_GET_ARG_32(2);
  uint32_t optname = SHIM_GET_ARG_32(3);
  uint32_t optval_ptr = SHIM_GET_ARG_32(4);
  uint32_t optlen = SHIM_GET_ARG_32(5);

  XELOGD("NetDll_setsockopt(%d, %.8X, %d, %d, %.8X, %d)", arg0, socket_handle,
         level, optname, optval_ptr, optlen);

  char* optval = reinterpret_cast<char*>(SHIM_MEM_ADDR(optval_ptr));
  int ret = setsockopt(socket_handle, level, optname, optval, optlen);

  SHIM_SET_RETURN_32(ret);
}

SHIM_CALL NetDll_ioctlsocket_shim(PPCContext* ppc_context,
                                  KernelState* kernel_state) {
  uint32_t arg0 = SHIM_GET_ARG_32(0);
  uint32_t socket_handle = SHIM_GET_ARG_32(1);
  uint32_t cmd = SHIM_GET_ARG_32(2);
  uint32_t arg_ptr = SHIM_GET_ARG_32(3);

  XELOGD("NetDll_ioctlsocket(%d, %.8X, %.8X, %.8X)", arg0, socket_handle, cmd,
         arg_ptr);

  u_long arg = SHIM_MEM_32(arg_ptr);
  int ret = ioctlsocket(socket_handle, cmd, &arg);

  SHIM_SET_MEM_32(arg_ptr, arg);
  SHIM_SET_RETURN_32(ret);
}

SHIM_CALL NetDll_bind_shim(PPCContext* ppc_context, KernelState* kernel_state) {
  uint32_t arg0 = SHIM_GET_ARG_32(0);
  uint32_t socket_handle = SHIM_GET_ARG_32(1);
  uint32_t name_ptr = SHIM_GET_ARG_32(2);
  uint32_t namelen = SHIM_GET_ARG_32(3);

  XELOGD("NetDll_bind(%d, %.8X, %.8X, %d)", arg0, socket_handle, name_ptr,
         namelen);

  sockaddr name;
  LoadSockaddr(SHIM_MEM_ADDR(name_ptr), &name);
  int ret = bind(socket_handle, &name, namelen);

  SHIM_SET_RETURN_32(ret);
}

SHIM_CALL NetDll_connect_shim(PPCContext* ppc_context,
                              KernelState* kernel_state) {
  uint32_t arg0 = SHIM_GET_ARG_32(0);
  uint32_t socket_handle = SHIM_GET_ARG_32(1);
  uint32_t name_ptr = SHIM_GET_ARG_32(2);
  uint32_t namelen = SHIM_GET_ARG_32(3);

  XELOGD("NetDll_connect(%d, %.8X, %.8X, %d)", arg0, socket_handle, name_ptr,
         namelen);

  sockaddr name;
  LoadSockaddr(SHIM_MEM_ADDR(name_ptr), &name);
  int ret = connect(socket_handle, &name, namelen);

  SHIM_SET_RETURN_32(ret);
}

SHIM_CALL NetDll_listen_shim(PPCContext* ppc_context,
                             KernelState* kernel_state) {
  uint32_t arg0 = SHIM_GET_ARG_32(0);
  uint32_t socket_handle = SHIM_GET_ARG_32(1);
  int32_t backlog = SHIM_GET_ARG_32(2);

  XELOGD("NetDll_listen(%d, %.8X, %d)", arg0, socket_handle, backlog);

  int ret = listen(socket_handle, backlog);

  SHIM_SET_RETURN_32(ret);
}

SHIM_CALL NetDll_accept_shim(PPCContext* ppc_context,
                             KernelState* kernel_state) {
  uint32_t arg0 = SHIM_GET_ARG_32(0);
  uint32_t socket_handle = SHIM_GET_ARG_32(1);
  uint32_t addr_ptr = SHIM_GET_ARG_32(2);
  uint32_t addrlen_ptr = SHIM_GET_ARG_32(3);

  XELOGD("NetDll_accept(%d, %.8X, %d)", arg0, socket_handle, addr_ptr,
         addrlen_ptr);

  sockaddr addr;
  int addrlen;
  SOCKET ret_socket = accept(socket_handle, &addr, &addrlen);

  if (ret_socket == INVALID_SOCKET) {
    std::memset(SHIM_MEM_ADDR(addr_ptr), 0, sizeof(addr));
    SHIM_SET_MEM_32(addrlen_ptr, sizeof(addr));
    SHIM_SET_RETURN_32(-1);
  } else {
    assert_true(ret_socket >> 32 == 0);
    StoreSockaddr(addr, SHIM_MEM_ADDR(addr_ptr));
    SHIM_SET_MEM_32(addrlen_ptr, addrlen);
    SHIM_SET_RETURN_32(static_cast<uint32_t>(ret_socket));
  }
}

void LoadFdset(const uint8_t* src, fd_set* dest) {
  dest->fd_count = xe::load_and_swap<uint32_t>(src);
  for (int i = 0; i < 64; ++i) {
    auto socket_handle =
        static_cast<SOCKET>(xe::load_and_swap<uint32_t>(src + 4 + i * 4));
    dest->fd_array[i] = socket_handle;
  }
}

void StoreFdset(const fd_set& src, uint8_t* dest) {
  xe::store_and_swap<uint32_t>(dest, src.fd_count);
  for (int i = 0; i < 64; ++i) {
    SOCKET socket_handle = src.fd_array[i];
    assert_true(socket_handle >> 32 == 0);
    xe::store_and_swap<uint32_t>(dest + 4 + i * 4,
                                 static_cast<uint32_t>(socket_handle));
  }
}

SHIM_CALL NetDll_select_shim(PPCContext* ppc_context,
                             KernelState* kernel_state) {
  uint32_t arg0 = SHIM_GET_ARG_32(0);
  uint32_t nfds = SHIM_GET_ARG_32(1);
  uint32_t readfds_ptr = SHIM_GET_ARG_32(2);
  uint32_t writefds_ptr = SHIM_GET_ARG_32(3);
  uint32_t exceptfds_ptr = SHIM_GET_ARG_32(4);
  uint32_t timeout_ptr = SHIM_GET_ARG_32(5);

  XELOGD("NetDll_select(%d, %d, %.8X, %.8X, %.8X, %.8X)", arg0, nfds,
         readfds_ptr, writefds_ptr, exceptfds_ptr, timeout_ptr);

  fd_set readfds = {0};
  if (readfds_ptr) {
    LoadFdset(SHIM_MEM_ADDR(readfds_ptr), &readfds);
  }
  fd_set writefds = {0};
  if (writefds_ptr) {
    LoadFdset(SHIM_MEM_ADDR(writefds_ptr), &writefds);
  }
  fd_set exceptfds = {0};
  if (exceptfds_ptr) {
    LoadFdset(SHIM_MEM_ADDR(exceptfds_ptr), &exceptfds);
  }
  timeval* timeout_in = nullptr;
  timeval timeout;
  if (timeout_ptr) {
    timeout = {static_cast<long>(SHIM_MEM_32(timeout_ptr + 0)),
               static_cast<long>(SHIM_MEM_32(timeout_ptr + 4))};
    Clock::ScaleGuestDurationTimeval(&timeout.tv_sec, &timeout.tv_usec);
    timeout_in = &timeout;
  }
  int ret = select(nfds, readfds_ptr ? &readfds : nullptr,
                   writefds_ptr ? &writefds : nullptr,
                   exceptfds_ptr ? &exceptfds : nullptr, timeout_in);
  if (readfds_ptr) {
    StoreFdset(readfds, SHIM_MEM_ADDR(readfds_ptr));
  }
  if (writefds_ptr) {
    StoreFdset(writefds, SHIM_MEM_ADDR(writefds_ptr));
  }
  if (exceptfds_ptr) {
    StoreFdset(exceptfds, SHIM_MEM_ADDR(exceptfds_ptr));
  }

  SHIM_SET_RETURN_32(ret);
}

SHIM_CALL NetDll_recv_shim(PPCContext* ppc_context, KernelState* kernel_state) {
  uint32_t arg0 = SHIM_GET_ARG_32(0);
  uint32_t socket_handle = SHIM_GET_ARG_32(1);
  uint32_t buf_ptr = SHIM_GET_ARG_32(2);
  uint32_t len = SHIM_GET_ARG_32(3);
  uint32_t flags = SHIM_GET_ARG_32(4);

  XELOGD("NetDll_recv(%d, %.8X, %.8X, %d, %d)", arg0, socket_handle, buf_ptr,
         len, flags);

  int ret = recv(socket_handle, reinterpret_cast<char*>(SHIM_MEM_ADDR(buf_ptr)),
                 len, flags);

  SHIM_SET_RETURN_32(ret);
}

SHIM_CALL NetDll_recvfrom_shim(PPCContext* ppc_context,
                               KernelState* kernel_state) {
  uint32_t arg0 = SHIM_GET_ARG_32(0);
  uint32_t socket_handle = SHIM_GET_ARG_32(1);
  uint32_t buf_ptr = SHIM_GET_ARG_32(2);
  uint32_t len = SHIM_GET_ARG_32(3);
  uint32_t flags = SHIM_GET_ARG_32(4);
  uint32_t from_ptr = SHIM_GET_ARG_32(5);
  uint32_t fromlen_ptr = SHIM_GET_ARG_32(6);

  XELOGD("NetDll_recvfrom(%d, %.8X, %.8X, %d, %d, %.8X, %.8X)", arg0,
         socket_handle, buf_ptr, len, flags, from_ptr, fromlen_ptr);

  sockaddr from;
  int fromlen = sizeof(from);
  int ret =
      recvfrom(socket_handle, reinterpret_cast<char*>(SHIM_MEM_ADDR(buf_ptr)),
               len, flags, &from, &fromlen);
  if (ret == -1) {
    std::memset(SHIM_MEM_ADDR(from_ptr), 0, sizeof(from));
    SHIM_SET_MEM_32(fromlen_ptr, sizeof(from));
  } else {
    StoreSockaddr(from, SHIM_MEM_ADDR(from_ptr));
    SHIM_SET_MEM_32(fromlen_ptr, fromlen);
  }

  SHIM_SET_RETURN_32(ret);
}

SHIM_CALL NetDll_send_shim(PPCContext* ppc_context, KernelState* kernel_state) {
  uint32_t arg0 = SHIM_GET_ARG_32(0);
  uint32_t socket_handle = SHIM_GET_ARG_32(1);
  uint32_t buf_ptr = SHIM_GET_ARG_32(2);
  uint32_t len = SHIM_GET_ARG_32(3);
  uint32_t flags = SHIM_GET_ARG_32(4);

  XELOGD("NetDll_send(%d, %.8X, %.8X, %d, %d)", arg0, socket_handle, buf_ptr,
         len, flags);

  int ret = send(socket_handle, reinterpret_cast<char*>(SHIM_MEM_ADDR(buf_ptr)),
                 len, flags);

  SHIM_SET_RETURN_32(ret);
}

SHIM_CALL NetDll_sendto_shim(PPCContext* ppc_context,
                             KernelState* kernel_state) {
  uint32_t arg0 = SHIM_GET_ARG_32(0);
  uint32_t socket_handle = SHIM_GET_ARG_32(1);
  uint32_t buf_ptr = SHIM_GET_ARG_32(2);
  uint32_t len = SHIM_GET_ARG_32(3);
  uint32_t flags = SHIM_GET_ARG_32(4);
  uint32_t to_ptr = SHIM_GET_ARG_32(5);
  uint32_t tolen = SHIM_GET_ARG_32(6);

  XELOGD("NetDll_sendto(%d, %.8X, %.8X, %d, %d, %.8X, %d)", arg0, socket_handle,
         buf_ptr, len, flags, to_ptr, tolen);

  sockaddr to;
  LoadSockaddr(SHIM_MEM_ADDR(to_ptr), &to);
  int ret =
      sendto(socket_handle, reinterpret_cast<char*>(SHIM_MEM_ADDR(buf_ptr)),
             len, flags, &to, tolen);

  SHIM_SET_RETURN_32(ret);
}

}  // namespace kernel
}  // namespace xe

void xe::kernel::xam::RegisterNetExports(
    xe::cpu::ExportResolver* export_resolver, KernelState* kernel_state) {
  SHIM_SET_MAPPING("xam.xex", NetDll_XNetStartup, state);
  SHIM_SET_MAPPING("xam.xex", NetDll_XNetCleanup, state);
  SHIM_SET_MAPPING("xam.xex", NetDll_XNetRandom, state);
  SHIM_SET_MAPPING("xam.xex", NetDll_WSAStartup, state);
  SHIM_SET_MAPPING("xam.xex", NetDll_WSACleanup, state);
  SHIM_SET_MAPPING("xam.xex", NetDll_WSAGetLastError, state);
  SHIM_SET_MAPPING("xam.xex", NetDll_XNetGetTitleXnAddr, state);
  SHIM_SET_MAPPING("xam.xex", NetDll_XNetGetEthernetLinkStatus, state);
  SHIM_SET_MAPPING("xam.xex", NetDll_inet_addr, state);
  SHIM_SET_MAPPING("xam.xex", NetDll_socket, state);
  SHIM_SET_MAPPING("xam.xex", NetDll_closesocket, state);
  SHIM_SET_MAPPING("xam.xex", NetDll_setsockopt, state);
  SHIM_SET_MAPPING("xam.xex", NetDll_ioctlsocket, state);
  SHIM_SET_MAPPING("xam.xex", NetDll_bind, state);
  SHIM_SET_MAPPING("xam.xex", NetDll_connect, state);
  SHIM_SET_MAPPING("xam.xex", NetDll_listen, state);
  SHIM_SET_MAPPING("xam.xex", NetDll_accept, state);
  SHIM_SET_MAPPING("xam.xex", NetDll_select, state);
  SHIM_SET_MAPPING("xam.xex", NetDll_recv, state);
  SHIM_SET_MAPPING("xam.xex", NetDll_recvfrom, state);
  SHIM_SET_MAPPING("xam.xex", NetDll_send, state);
  SHIM_SET_MAPPING("xam.xex", NetDll_sendto, state);
}
