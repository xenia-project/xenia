/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <cstring>

#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xam_private.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_error.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_threading.h"
#include "xenia/kernel/xevent.h"
#include "xenia/kernel/xthread.h"
#include "xenia/xbox.h"

// NOTE: must be included last as it expects windows.h to already be included.
#define _WINSOCK_DEPRECATED_NO_WARNINGS  // inet_addr
#include <winsock2.h>                    // NOLINT(build/include_order)

namespace xe {
namespace kernel {
namespace xam {

// https://github.com/G91/TitanOffLine/blob/1e692d9bb9dfac386d08045ccdadf4ae3227bb5e/xkelib/xam/xamNet.h
enum {
  XNCALLER_INVALID = 0x0,
  XNCALLER_TITLE = 0x1,
  XNCALLER_SYSAPP = 0x2,
  XNCALLER_XBDM = 0x3,
  XNCALLER_TEST = 0x4,
  NUM_XNCALLER_TYPES = 0x4,
};

// https://github.com/pmrowla/hl2sdk-csgo/blob/master/common/xbox/xboxstubs.h
typedef struct {
  // FYI: IN_ADDR should be in network-byte order.
  IN_ADDR ina;                   // IP address (zero if not static/DHCP)
  IN_ADDR inaOnline;             // Online IP address (zero if not online)
  xe::be<uint16_t> wPortOnline;  // Online port
  uint8_t abEnet[6];             // Ethernet MAC address
  uint8_t abOnline[20];          // Online identification
} XNADDR;

struct Xsockaddr_t {
  xe::be<uint16_t> sa_family;
  char sa_data[14];
};

struct XWSABUF {
  xe::be<uint32_t> len;
  xe::be<uint32_t> buf_ptr;
};

struct XWSAOVERLAPPED {
  xe::be<uint32_t> internal;
  xe::be<uint32_t> internal_high;
  union {
    struct {
      xe::be<uint32_t> offset;
      xe::be<uint32_t> offset_high;
    };
    xe::be<uint32_t> pointer;
  };
  xe::be<uint32_t> event_handle;
};

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

// https://github.com/joolswills/mameox/blob/master/MAMEoX/Sources/xbox_Network.cpp#L136
struct XNetStartupParams {
  BYTE cfgSizeOfStruct;
  BYTE cfgFlags;
  BYTE cfgSockMaxDgramSockets;
  BYTE cfgSockMaxStreamSockets;
  BYTE cfgSockDefaultRecvBufsizeInK;
  BYTE cfgSockDefaultSendBufsizeInK;
  BYTE cfgKeyRegMax;
  BYTE cfgSecRegMax;
  BYTE cfgQosDataLimitDiv4;
  BYTE cfgQosProbeTimeoutInSeconds;
  BYTE cfgQosProbeRetries;
  BYTE cfgQosSrvMaxSimultaneousResponses;
  BYTE cfgQosPairWaitTimeInSeconds;
};

XNetStartupParams xnet_startup_params = {0};

SHIM_CALL NetDll_XNetStartup_shim(PPCContext* ppc_context,
                                  KernelState* kernel_state) {
  uint32_t caller = SHIM_GET_ARG_32(0);
  uint32_t params_ptr = SHIM_GET_ARG_32(1);

  XELOGD("NetDll_XNetStartup(%d, %.8X)", caller, params_ptr);

  if (params_ptr) {
    auto params =
        kernel_memory()->TranslateVirtual<XNetStartupParams*>(params_ptr);
    assert_true(params->cfgSizeOfStruct == sizeof(XNetStartupParams));
    std::memcpy(&xnet_startup_params, params, sizeof(XNetStartupParams));
  }

  SHIM_SET_RETURN_32(0);
}

SHIM_CALL NetDll_XNetCleanup_shim(PPCContext* ppc_context,
                                  KernelState* kernel_state) {
  uint32_t caller = SHIM_GET_ARG_32(0);
  uint32_t params_ptr = SHIM_GET_ARG_32(1);

  XELOGD("NetDll_XNetCleanup(%d, %.8X)", caller, params_ptr);

  SHIM_SET_RETURN_32(0);
}

dword_result_t NetDll_XNetGetOpt(dword_t one, dword_t option_id,
                                 lpvoid_t buffer_ptr, lpdword_t buffer_size) {
  assert_true(one == 1);
  switch (option_id) {
    case 1:
      if (*buffer_size < sizeof(XNetStartupParams)) {
        *buffer_size = sizeof(XNetStartupParams);
        return WSAEMSGSIZE;
      }
      std::memcpy(buffer_ptr, &xnet_startup_params, sizeof(XNetStartupParams));
      return 0;
    default:
      XELOGE("NetDll_XNetGetOpt: option %d unimplemented", option_id);
      return WSAEINVAL;
  }
}
DECLARE_XAM_EXPORT(NetDll_XNetGetOpt, ExportTag::kNetworking);

SHIM_CALL NetDll_XNetRandom_shim(PPCContext* ppc_context,
                                 KernelState* kernel_state) {
  uint32_t caller = SHIM_GET_ARG_32(0);
  uint32_t buffer_ptr = SHIM_GET_ARG_32(1);
  uint32_t length = SHIM_GET_ARG_32(2);

  XELOGD("NetDll_XNetRandom(%d, %.8X, %d)", caller, buffer_ptr, length);

  // For now, constant values.
  // This makes replicating things easier.
  std::memset(SHIM_MEM_ADDR(buffer_ptr), 0xBB, length);

  SHIM_SET_RETURN_32(0);
}

SHIM_CALL NetDll_WSAStartup_shim(PPCContext* ppc_context,
                                 KernelState* kernel_state) {
  uint32_t caller = SHIM_GET_ARG_32(0);
  uint16_t version = SHIM_GET_ARG_16(1);
  uint32_t data_ptr = SHIM_GET_ARG_32(2);

  XELOGD("NetDll_WSAStartup(%d, %.4X, %.8X)", caller, version, data_ptr);

  // TODO(benvanik): abstraction layer needed.
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
  uint32_t caller = SHIM_GET_ARG_32(0);

  XELOGD("NetDll_WSACleanup(%d)", caller);

  // Don't actually call WSACleanup - we use it for the debugger and such.
  // int ret = WSACleanup();
  int ret = 0;

  SHIM_SET_RETURN_32(ret);
}

SHIM_CALL NetDll_WSAGetLastError_shim(PPCContext* ppc_context,
                                      KernelState* kernel_state) {
  XELOGD("NetDll_WSAGetLastError()");

  int err = WSAGetLastError();
  SHIM_SET_RETURN_32(err);
}

dword_result_t NetDll_WSARecvFrom(dword_t caller, dword_t socket,
                                  pointer_t<XWSABUF> buffers_ptr,
                                  dword_t buffer_count,
                                  lpdword_t num_bytes_recv, lpdword_t flags_ptr,
                                  pointer_t<Xsockaddr_t> from_addr,
                                  pointer_t<XWSAOVERLAPPED> overlapped_ptr,
                                  lpvoid_t completion_routine_ptr) {
  if (overlapped_ptr) {
    auto evt = kernel_state()->object_table()->LookupObject<XEvent>(
        overlapped_ptr->event_handle);

    if (evt) {
      evt->Set(0, false);
    }
  }

  return 0;
}
DECLARE_XAM_EXPORT(NetDll_WSARecvFrom, ExportTag::kNetworking);

dword_result_t NetDll_WSAWaitForMultipleEvents(
    dword_t num_events, pointer_t<xe::be<uint32_t>> events, dword_t wait_all,
    dword_t timeout, dword_t alertable) {
  if (num_events > 64) {
    XThread::GetCurrentThread()->set_last_error(87);  // ERROR_INVALID_PARAMETER
    return ~0u;
  }

  xe::be<uint64_t> timeout_wait = (uint64_t)timeout;

  X_STATUS result = 0;
  do {
    result = xboxkrnl::NtWaitForMultipleObjectsEx(
        num_events, events, wait_all, 1, alertable,
        timeout != -1 ? &timeout_wait : nullptr);
  } while (result == X_STATUS_ALERTED);

  if (XFAILED(result)) {
    uint32_t error = xboxkrnl::RtlNtStatusToDosError(result);
    XThread::GetCurrentThread()->set_last_error(error);
    return ~0u;
  }
  return 0;
}
DECLARE_XAM_EXPORT(NetDll_WSAWaitForMultipleEvents,
                   ExportTag::kNetworking | ExportTag::kThreading);

dword_result_t NetDll_WSACreateEvent() {
  XEvent* ev = new XEvent(kernel_state());
  ev->Initialize(true, false);
  return ev->handle();
}
DECLARE_XAM_EXPORT(NetDll_WSACreateEvent,
                   ExportTag::kNetworking | ExportTag::kThreading);

dword_result_t NetDll_WSACloseEvent(dword_t event_handle) {
  X_STATUS result = kernel_state()->object_table()->ReleaseHandle(event_handle);
  if (XFAILED(result)) {
    uint32_t error = xboxkrnl::RtlNtStatusToDosError(result);
    XThread::GetCurrentThread()->set_last_error(error);
    return 0;
  }
  return 1;
}
DECLARE_XAM_EXPORT(NetDll_WSACloseEvent,
                   ExportTag::kNetworking | ExportTag::kThreading);

dword_result_t NetDll_WSAResetEvent(dword_t event_handle) {
  X_STATUS result = xboxkrnl::NtClearEvent(event_handle);
  if (XFAILED(result)) {
    uint32_t error = xboxkrnl::RtlNtStatusToDosError(result);
    XThread::GetCurrentThread()->set_last_error(error);
    return 0;
  }
  return 1;
}
DECLARE_XAM_EXPORT(NetDll_WSAResetEvent,
                   ExportTag::kNetworking | ExportTag::kThreading);

dword_result_t NetDll_WSASetEvent(dword_t event_handle) {
  X_STATUS result = xboxkrnl::NtSetEvent(event_handle, nullptr);
  if (XFAILED(result)) {
    uint32_t error = xboxkrnl::RtlNtStatusToDosError(result);
    XThread::GetCurrentThread()->set_last_error(error);
    return 0;
  }
  return 1;
}
DECLARE_XAM_EXPORT(NetDll_WSASetEvent,
                   ExportTag::kNetworking | ExportTag::kThreading);

struct XnAddrStatus {
  // Address acquisition is not yet complete
  static const uint32_t XNET_GET_XNADDR_PENDING = 0x00000000;
  // XNet is uninitialized or no debugger found
  static const uint32_t XNET_GET_XNADDR_NONE = 0x00000001;
  // Host has ethernet address (no IP address)
  static const uint32_t XNET_GET_XNADDR_ETHERNET = 0x00000002;
  // Host has statically assigned IP address
  static const uint32_t XNET_GET_XNADDR_STATIC = 0x00000004;
  // Host has DHCP assigned IP address
  static const uint32_t XNET_GET_XNADDR_DHCP = 0x00000008;
  // Host has PPPoE assigned IP address
  static const uint32_t XNET_GET_XNADDR_PPPOE = 0x00000010;
  // Host has one or more gateways configured
  static const uint32_t XNET_GET_XNADDR_GATEWAY = 0x00000020;
  // Host has one or more DNS servers configured
  static const uint32_t XNET_GET_XNADDR_DNS = 0x00000040;
  // Host is currently connected to online service
  static const uint32_t XNET_GET_XNADDR_ONLINE = 0x00000080;
  // Network configuration requires troubleshooting
  static const uint32_t XNET_GET_XNADDR_TROUBLESHOOT = 0x00008000;
};

dword_result_t NetDll_XNetGetTitleXnAddr(dword_t caller,
                                         pointer_t<XNADDR> addr_ptr) {
  // Just return a loopback address atm.
  addr_ptr->ina.S_un.S_addr = htonl(INADDR_LOOPBACK);
  addr_ptr->inaOnline.S_un.S_addr = 0;
  addr_ptr->wPortOnline = 0;
  std::memset(addr_ptr->abEnet, 0, 6);
  std::memset(addr_ptr->abOnline, 0, 20);

  return XnAddrStatus::XNET_GET_XNADDR_STATIC;
}
DECLARE_XAM_EXPORT(NetDll_XNetGetTitleXnAddr,
                   ExportTag::kNetworking | ExportTag::kStub);

dword_result_t NetDll_XNetGetDebugXnAddr(dword_t caller,
                                         pointer_t<XNADDR> addr_ptr) {
  addr_ptr.Zero();

  // XNET_GET_XNADDR_NONE causes caller to gracefully return.
  return XnAddrStatus::XNET_GET_XNADDR_NONE;
}
DECLARE_XAM_EXPORT(NetDll_XNetGetDebugXnAddr,
                   ExportTag::kNetworking | ExportTag::kStub);

// http://www.google.com/patents/WO2008112448A1?cl=en
// Reserves a port for use by system link
dword_result_t NetDll_XNetSetSystemLinkPort(dword_t caller, dword_t port) {
  return 1;
}
DECLARE_XAM_EXPORT(NetDll_XNetSetSystemLinkPort,
                   ExportTag::kNetworking | ExportTag::kStub);

SHIM_CALL NetDll_XNetGetEthernetLinkStatus_shim(PPCContext* ppc_context,
                                                KernelState* kernel_state) {
  // Games seem to call this before *Startup. If we return 0, they don't even
  // try.
  uint32_t caller = SHIM_GET_ARG_32(0);

  XELOGD("NetDll_XNetGetEthernetLinkStatus(%d)", caller);

  SHIM_SET_RETURN_32(0);
}

SHIM_CALL NetDll_XNetQosServiceLookup_shim(PPCContext* ppc_context,
                                           KernelState* kernel_state) {
  uint32_t caller = SHIM_GET_ARG_32(0);
  uint32_t zero = SHIM_GET_ARG_32(1);
  uint32_t event_handle = SHIM_GET_ARG_32(2);
  uint32_t out_ptr = SHIM_GET_ARG_32(3);

  XELOGD("NetDll_XNetQosServiceLookup(%d, %d, %.8X, %.8X)", caller, zero,
         event_handle, out_ptr);

  // Non-zero is error.
  SHIM_SET_RETURN_32(1);
}

dword_result_t NetDll_inet_addr(lpstring_t addr_ptr) {
  uint32_t addr = inet_addr(addr_ptr);
  return xe::byte_swap(addr);
}
DECLARE_XAM_EXPORT(NetDll_inet_addr,
                   ExportTag::kImplemented | ExportTag::kNetworking);

SHIM_CALL NetDll_socket_shim(PPCContext* ppc_context,
                             KernelState* kernel_state) {
  uint32_t caller = SHIM_GET_ARG_32(0);
  uint32_t af = SHIM_GET_ARG_32(1);
  uint32_t type = SHIM_GET_ARG_32(2);
  uint32_t protocol = SHIM_GET_ARG_32(3);

  XELOGD("NetDll_socket(%d, %d, %d, %d)", caller, af, type, protocol);
  if (protocol == 0xFE) {
    protocol = IPPROTO_UDP;
  }

  SOCKET socket_handle = socket(af, type, protocol);
  assert_true(socket_handle >> 32 == 0);

  XELOGD("NetDll_socket = %.8X", socket_handle);

  SHIM_SET_RETURN_32(static_cast<uint32_t>(socket_handle));
}

SHIM_CALL NetDll_closesocket_shim(PPCContext* ppc_context,
                                  KernelState* kernel_state) {
  uint32_t caller = SHIM_GET_ARG_32(0);
  uint32_t socket_handle = SHIM_GET_ARG_32(1);

  XELOGD("NetDll_closesocket(%d, %.8X)", caller, socket_handle);
  int ret = closesocket(socket_handle);

  SHIM_SET_RETURN_32(ret);
}

SHIM_CALL NetDll_setsockopt_shim(PPCContext* ppc_context,
                                 KernelState* kernel_state) {
  uint32_t caller = SHIM_GET_ARG_32(0);
  uint32_t socket_handle = SHIM_GET_ARG_32(1);
  uint32_t level = SHIM_GET_ARG_32(2);
  uint32_t optname = SHIM_GET_ARG_32(3);
  uint32_t optval_ptr = SHIM_GET_ARG_32(4);
  uint32_t optlen = SHIM_GET_ARG_32(5);

  XELOGD("NetDll_setsockopt(%d, %.8X, %d, %d, %.8X, %d)", caller, socket_handle,
         level, optname, optval_ptr, optlen);

  char* optval = reinterpret_cast<char*>(SHIM_MEM_ADDR(optval_ptr));
  int ret = setsockopt(socket_handle, level, optname, optval, optlen);

  SHIM_SET_RETURN_32(ret);
}

SHIM_CALL NetDll_ioctlsocket_shim(PPCContext* ppc_context,
                                  KernelState* kernel_state) {
  uint32_t caller = SHIM_GET_ARG_32(0);
  uint32_t socket_handle = SHIM_GET_ARG_32(1);
  uint32_t cmd = SHIM_GET_ARG_32(2);
  uint32_t arg_ptr = SHIM_GET_ARG_32(3);

  XELOGD("NetDll_ioctlsocket(%d, %.8X, %.8X, %.8X)", caller, socket_handle, cmd,
         arg_ptr);

  u_long arg = SHIM_MEM_32(arg_ptr);
  int ret = ioctlsocket(socket_handle, cmd, &arg);

  SHIM_SET_MEM_32(arg_ptr, arg);
  SHIM_SET_RETURN_32(ret);
}

SHIM_CALL NetDll_bind_shim(PPCContext* ppc_context, KernelState* kernel_state) {
  uint32_t caller = SHIM_GET_ARG_32(0);
  uint32_t socket_handle = SHIM_GET_ARG_32(1);
  uint32_t name_ptr = SHIM_GET_ARG_32(2);
  uint32_t namelen = SHIM_GET_ARG_32(3);

  XELOGD("NetDll_bind(%d, %.8X, %.8X, %d)", caller, socket_handle, name_ptr,
         namelen);

  sockaddr name;
  LoadSockaddr(SHIM_MEM_ADDR(name_ptr), &name);
  int ret = bind(socket_handle, &name, namelen);

  SHIM_SET_RETURN_32(ret);
}

SHIM_CALL NetDll_connect_shim(PPCContext* ppc_context,
                              KernelState* kernel_state) {
  uint32_t caller = SHIM_GET_ARG_32(0);
  uint32_t socket_handle = SHIM_GET_ARG_32(1);
  uint32_t name_ptr = SHIM_GET_ARG_32(2);
  uint32_t namelen = SHIM_GET_ARG_32(3);

  XELOGD("NetDll_connect(%d, %.8X, %.8X, %d)", caller, socket_handle, name_ptr,
         namelen);

  sockaddr name;
  LoadSockaddr(SHIM_MEM_ADDR(name_ptr), &name);
  int ret = connect(socket_handle, &name, namelen);

  SHIM_SET_RETURN_32(ret);
}

SHIM_CALL NetDll_listen_shim(PPCContext* ppc_context,
                             KernelState* kernel_state) {
  uint32_t caller = SHIM_GET_ARG_32(0);
  uint32_t socket_handle = SHIM_GET_ARG_32(1);
  int32_t backlog = SHIM_GET_ARG_32(2);

  XELOGD("NetDll_listen(%d, %.8X, %d)", caller, socket_handle, backlog);

  int ret = listen(socket_handle, backlog);

  SHIM_SET_RETURN_32(ret);
}

SHIM_CALL NetDll_accept_shim(PPCContext* ppc_context,
                             KernelState* kernel_state) {
  uint32_t caller = SHIM_GET_ARG_32(0);
  uint32_t socket_handle = SHIM_GET_ARG_32(1);
  uint32_t addr_ptr = SHIM_GET_ARG_32(2);
  uint32_t addrlen_ptr = SHIM_GET_ARG_32(3);

  XELOGD("NetDll_accept(%d, %.8X, %d)", caller, socket_handle, addr_ptr,
         addrlen_ptr);

  sockaddr addr;
  int addrlen;
  SOCKET ret_socket = accept(socket_handle, &addr, &addrlen);

  if (ret_socket == INVALID_SOCKET) {
    if (addr_ptr) {
      std::memset(SHIM_MEM_ADDR(addr_ptr), 0, sizeof(addr));
    }

    if (addrlen_ptr) {
      SHIM_SET_MEM_32(addrlen_ptr, sizeof(addr));
    }

    SHIM_SET_RETURN_32(-1);
  } else {
    assert_true(ret_socket >> 32 == 0);
    if (addr_ptr) {
      StoreSockaddr(addr, SHIM_MEM_ADDR(addr_ptr));
    }

    if (addrlen_ptr) {
      SHIM_SET_MEM_32(addrlen_ptr, addrlen);
    }

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
  uint32_t caller = SHIM_GET_ARG_32(0);
  uint32_t nfds = SHIM_GET_ARG_32(1);
  uint32_t readfds_ptr = SHIM_GET_ARG_32(2);
  uint32_t writefds_ptr = SHIM_GET_ARG_32(3);
  uint32_t exceptfds_ptr = SHIM_GET_ARG_32(4);
  uint32_t timeout_ptr = SHIM_GET_ARG_32(5);

  XELOGD("NetDll_select(%d, %d, %.8X, %.8X, %.8X, %.8X)", caller, nfds,
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
    timeout = {static_cast<int32_t>(SHIM_MEM_32(timeout_ptr + 0)),
               static_cast<int32_t>(SHIM_MEM_32(timeout_ptr + 4))};
    Clock::ScaleGuestDurationTimeval(
        reinterpret_cast<int32_t*>(&timeout.tv_sec),
        reinterpret_cast<int32_t*>(&timeout.tv_usec));
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
  uint32_t caller = SHIM_GET_ARG_32(0);
  uint32_t socket_handle = SHIM_GET_ARG_32(1);
  uint32_t buf_ptr = SHIM_GET_ARG_32(2);
  uint32_t len = SHIM_GET_ARG_32(3);
  uint32_t flags = SHIM_GET_ARG_32(4);

  XELOGD("NetDll_recv(%d, %.8X, %.8X, %d, %d)", caller, socket_handle, buf_ptr,
         len, flags);

  int ret = recv(socket_handle, reinterpret_cast<char*>(SHIM_MEM_ADDR(buf_ptr)),
                 len, flags);

  SHIM_SET_RETURN_32(ret);
}

SHIM_CALL NetDll_recvfrom_shim(PPCContext* ppc_context,
                               KernelState* kernel_state) {
  uint32_t caller = SHIM_GET_ARG_32(0);
  uint32_t socket_handle = SHIM_GET_ARG_32(1);
  uint32_t buf_ptr = SHIM_GET_ARG_32(2);
  uint32_t len = SHIM_GET_ARG_32(3);
  uint32_t flags = SHIM_GET_ARG_32(4);
  uint32_t from_ptr = SHIM_GET_ARG_32(5);
  uint32_t fromlen_ptr = SHIM_GET_ARG_32(6);

  XELOGD("NetDll_recvfrom(%d, %.8X, %.8X, %d, %d, %.8X, %.8X)", caller,
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
  uint32_t caller = SHIM_GET_ARG_32(0);
  uint32_t socket_handle = SHIM_GET_ARG_32(1);
  uint32_t buf_ptr = SHIM_GET_ARG_32(2);
  uint32_t len = SHIM_GET_ARG_32(3);
  uint32_t flags = SHIM_GET_ARG_32(4);

  XELOGD("NetDll_send(%d, %.8X, %.8X, %d, %d)", caller, socket_handle, buf_ptr,
         len, flags);

  int ret = send(socket_handle, reinterpret_cast<char*>(SHIM_MEM_ADDR(buf_ptr)),
                 len, flags);

  SHIM_SET_RETURN_32(ret);
}

SHIM_CALL NetDll_sendto_shim(PPCContext* ppc_context,
                             KernelState* kernel_state) {
  uint32_t caller = SHIM_GET_ARG_32(0);
  uint32_t socket_handle = SHIM_GET_ARG_32(1);
  uint32_t buf_ptr = SHIM_GET_ARG_32(2);
  uint32_t len = SHIM_GET_ARG_32(3);
  uint32_t flags = SHIM_GET_ARG_32(4);
  uint32_t to_ptr = SHIM_GET_ARG_32(5);
  uint32_t tolen = SHIM_GET_ARG_32(6);

  XELOGD("NetDll_sendto(%d, %.8X, %.8X, %d, %d, %.8X, %d)", caller,
         socket_handle, buf_ptr, len, flags, to_ptr, tolen);

  sockaddr to;
  LoadSockaddr(SHIM_MEM_ADDR(to_ptr), &to);
  int ret =
      sendto(socket_handle, reinterpret_cast<char*>(SHIM_MEM_ADDR(buf_ptr)),
             len, flags, &to, tolen);

  SHIM_SET_RETURN_32(ret);
}

void RegisterNetExports(xe::cpu::ExportResolver* export_resolver,
                        KernelState* kernel_state) {
  SHIM_SET_MAPPING("xam.xex", NetDll_XNetStartup, state);
  SHIM_SET_MAPPING("xam.xex", NetDll_XNetCleanup, state);
  SHIM_SET_MAPPING("xam.xex", NetDll_XNetRandom, state);
  SHIM_SET_MAPPING("xam.xex", NetDll_WSAStartup, state);
  SHIM_SET_MAPPING("xam.xex", NetDll_WSACleanup, state);
  SHIM_SET_MAPPING("xam.xex", NetDll_WSAGetLastError, state);
  SHIM_SET_MAPPING("xam.xex", NetDll_XNetGetEthernetLinkStatus, state);
  SHIM_SET_MAPPING("xam.xex", NetDll_XNetQosServiceLookup, state);
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

}  // namespace xam
}  // namespace kernel
}  // namespace xe
