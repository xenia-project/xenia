/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <winsock2.h>

#include <xenia/kernel/xam_net.h>

#include <xenia/kernel/kernel_state.h>
#include <xenia/kernel/xam_private.h>
#include <xenia/kernel/util/shim_utils.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xam;


namespace xe {
namespace kernel {


SHIM_CALL NetDll_XNetStartup_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t arg0 = SHIM_GET_ARG_32(0);
  uint32_t params_ptr = SHIM_GET_ARG_32(1);

  XELOGD(
      "NetDll_XNetStartup(%d, %.8X)",
      arg0,
      params_ptr);

  SHIM_SET_RETURN_64(0);
}

SHIM_CALL NetDll_XNetCleanup_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t arg0 = SHIM_GET_ARG_32(0);
  uint32_t params_ptr = SHIM_GET_ARG_32(1);

  XELOGD(
      "NetDll_XNetCleanup(%d, %.8X)",
      arg0,
      params_ptr);

  SHIM_SET_RETURN_64(0);
}

SHIM_CALL NetDll_XNetRandom_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t arg0 = SHIM_GET_ARG_32(0);
  uint32_t buffer_ptr = SHIM_GET_ARG_32(1);
  uint32_t length = SHIM_GET_ARG_32(2);

  XELOGD(
      "NetDll_XNetRandom(%d, %.8X, %d)",
      arg0,
      buffer_ptr,
      length);

  // For now, constant values.
  // This makes replicating things easier.
  memset(SHIM_MEM_ADDR(buffer_ptr), 0xBB, length);

  SHIM_SET_RETURN_64(0);
}

SHIM_CALL NetDll_WSAStartup_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t arg0 = SHIM_GET_ARG_32(0);
  uint32_t version = SHIM_GET_ARG_16(1);
  uint32_t data_ptr = SHIM_GET_ARG_32(2);

  XELOGD(
      "NetDll_WSAStartup(%d, %.4X, %.8X)",
      arg0,
      version,
      data_ptr);

  if (data_ptr) {
    SHIM_SET_MEM_16(data_ptr + 0x000, version);
    SHIM_SET_MEM_16(data_ptr + 0x002, version);
    SHIM_SET_MEM_32(data_ptr + 0x004, 0);
    SHIM_SET_MEM_32(data_ptr + 0x105, 0);
    SHIM_SET_MEM_16(data_ptr + 0x186, 0);
    SHIM_SET_MEM_16(data_ptr + 0x188, 0);
    // Some games (PoG) want this value round-tripped - they'll compare if it
    // changes and bugcheck if it does.
    uint32_t vendor_ptr = SHIM_MEM_32(data_ptr + 0x190);
    SHIM_SET_MEM_32(data_ptr + 0x190, vendor_ptr);
  }

  SHIM_SET_RETURN_64(0);
}

SHIM_CALL NetDll_WSAGetLastError_shim(
    PPCContext* ppc_state, KernelState* state) {
  XELOGD("NetDll_WSAGetLastError()");
  SHIM_SET_RETURN_32(10093L);  // WSANOTINITIALISED
}

SHIM_CALL NetDll_XNetGetTitleXnAddr_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t arg0 = SHIM_GET_ARG_32(0);
  uint32_t arg1 = SHIM_GET_ARG_32(1);
  XELOGD(
      "NetDll_XNetGetTitleXnAddr(%d, %.8X)",
      arg0,
      arg1);
  SHIM_SET_RETURN_32(0x00000001);
}

SHIM_CALL NetDll_XNetGetEthernetLinkStatus_shim(
    PPCContext* ppc_state, KernelState* state) {
  // Games seem to call this before *Startup. If we return 0, they don't even
  // try.
  uint32_t arg0 = SHIM_GET_ARG_32(0);
  XELOGD(
      "NetDll_XNetGetEthernetLinkStatus(%d)",
      arg0);
  SHIM_SET_RETURN_32(0);
}

SHIM_CALL NetDll_inet_addr_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t cp_ptr = SHIM_GET_ARG_32(0);
  XELOGD(
      "NetDll_inet_addr(%.8X)",
      cp_ptr);
  SHIM_SET_RETURN_32(0xFFFFFFFF); // X_INADDR_NONE
}

SHIM_CALL NetDll_socket_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t arg0 = SHIM_GET_ARG_32(0);
  uint32_t af = SHIM_GET_ARG_32(1);
  uint32_t type = SHIM_GET_ARG_32(2);
  uint32_t protocol = SHIM_GET_ARG_32(3);
  XELOGD(
      "NetDll_socket(%d, %d, %d, %d)",
      arg0,
      af,
      type,
      protocol);
  SHIM_SET_RETURN_32(X_SOCKET_ERROR);
}

SHIM_CALL NetDll_setsockopt_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t arg0 = SHIM_GET_ARG_32(0);
  uint32_t socket_ptr = SHIM_GET_ARG_32(1);
  uint32_t level = SHIM_GET_ARG_32(2);
  uint32_t optname = SHIM_GET_ARG_32(3);
  uint32_t optval_ptr = SHIM_GET_ARG_32(4);
  uint32_t optlen = SHIM_GET_ARG_32(5);
  XELOGD(
      "NetDll_setsockopt(%d, %.8X, %d, %d, %.8X, %d)",
      arg0,
      socket_ptr,
      level,
      optname,
      optval_ptr,
      optlen);
  SHIM_SET_RETURN_32(X_SOCKET_ERROR);
}

SHIM_CALL NetDll_connect_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t socket_ptr = SHIM_GET_ARG_32(0);
  uint32_t sockaddr_ptr = SHIM_GET_ARG_32(1);
  uint32_t namelen = SHIM_GET_ARG_32(2);
  XELOGD(
      "NetDll_connect(%.8X, %.8X, %d)",
      socket_ptr,
      sockaddr_ptr,
      namelen);
  SHIM_SET_RETURN_32(X_SOCKET_ERROR);
}

SHIM_CALL NetDll_recv_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t arg0 = SHIM_GET_ARG_32(0);
  uint32_t socket_ptr = SHIM_GET_ARG_32(1);
  uint32_t buf_ptr = SHIM_GET_ARG_32(2);
  uint32_t len = SHIM_GET_ARG_32(3);
  uint32_t flags = SHIM_GET_ARG_32(4);
  XELOGD(
      "NetDll_recv(%d, %.8X, %.8X, %d, %d)",
      arg0,
      socket_ptr,
      buf_ptr,
      len,
      flags);
  SHIM_SET_RETURN_32(X_SOCKET_ERROR);
}

SHIM_CALL NetDll_recvfrom_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t arg0 = SHIM_GET_ARG_32(0);
  uint32_t socket_ptr = SHIM_GET_ARG_32(1);
  uint32_t buf_ptr = SHIM_GET_ARG_32(2);
  uint32_t len = SHIM_GET_ARG_32(3);
  uint32_t flags = SHIM_GET_ARG_32(4);
  uint32_t from_ptr = SHIM_GET_ARG_32(5);
  uint32_t fromlen_ptr = SHIM_GET_ARG_32(6);
  XELOGD(
      "NetDll_recvfrom(%d, %.8X, %.8X, %d, %d, %.8X, %.8X)",
      arg0,
      socket_ptr,
      buf_ptr,
      len,
      flags,
      from_ptr,
      fromlen_ptr);
  SHIM_SET_RETURN_32(X_SOCKET_ERROR);
}

SHIM_CALL NetDll_send_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t arg0 = SHIM_GET_ARG_32(0);
  uint32_t socket_ptr = SHIM_GET_ARG_32(1);
  uint32_t buf_ptr = SHIM_GET_ARG_32(2);
  uint32_t len = SHIM_GET_ARG_32(3);
  uint32_t flags = SHIM_GET_ARG_32(4);
  XELOGD(
      "NetDll_send(%d,%.8X, %.8X, %d, %d)",
      arg0,
      socket_ptr,
      buf_ptr,
      len,
      flags);
  SHIM_SET_RETURN_32(X_SOCKET_ERROR);
}


}  // namespace kernel
}  // namespace xe


void xe::kernel::xam::RegisterNetExports(
    ExportResolver* export_resolver, KernelState* state) {
  SHIM_SET_MAPPING("xam.xex", NetDll_XNetStartup, state);
  SHIM_SET_MAPPING("xam.xex", NetDll_XNetCleanup, state);
  SHIM_SET_MAPPING("xam.xex", NetDll_XNetRandom, state);
  SHIM_SET_MAPPING("xam.xex", NetDll_WSAStartup, state);
  SHIM_SET_MAPPING("xam.xex", NetDll_WSAGetLastError, state);
  SHIM_SET_MAPPING("xam.xex", NetDll_XNetGetTitleXnAddr, state);
  SHIM_SET_MAPPING("xam.xex", NetDll_XNetGetEthernetLinkStatus, state);
  SHIM_SET_MAPPING("xam.xex", NetDll_inet_addr, state);
  SHIM_SET_MAPPING("xam.xex", NetDll_socket, state);
  SHIM_SET_MAPPING("xam.xex", NetDll_setsockopt, state);
  SHIM_SET_MAPPING("xam.xex", NetDll_connect, state);
  SHIM_SET_MAPPING("xam.xex", NetDll_recv, state);
  SHIM_SET_MAPPING("xam.xex", NetDll_recvfrom, state);
  SHIM_SET_MAPPING("xam.xex", NetDll_send, state);
}
