/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/xam/xam_net.h>

#include <xenia/kernel/shim_utils.h>
#include <xenia/kernel/xam/xam_private.h>
#include <xenia/kernel/xam/xam_state.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xam;


namespace xe {
namespace kernel {
namespace xam {


SHIM_CALL NetDll_XNetStartup_shim(
    PPCContext* ppc_state, XamState* state) {
  uint32_t one = SHIM_GET_ARG_32(0);
  uint32_t params_ptr = SHIM_GET_ARG_32(1);

  XELOGD(
      "NetDll_XNetStartup(%d, %.8X)",
      one,
      params_ptr);

  SHIM_SET_RETURN(0);
}


SHIM_CALL NetDll_WSAStartup_shim(
    PPCContext* ppc_state, XamState* state) {
  uint32_t one = SHIM_GET_ARG_32(0);
  uint32_t version = SHIM_GET_ARG_16(1);
  uint32_t data_ptr = SHIM_GET_ARG_32(2);

  XELOGD(
      "NetDll_WSAStartup(%d, %.4X, %.8X)",
      one,
      version,
      data_ptr);

  if (data_ptr) {
    SHIM_SET_MEM_16(data_ptr + 0x000, version);
    SHIM_SET_MEM_16(data_ptr + 0x002, 0);
    SHIM_SET_MEM_32(data_ptr + 0x004, 0);
    SHIM_SET_MEM_32(data_ptr + 0x105, 0);
    SHIM_SET_MEM_16(data_ptr + 0x186, 0);
    SHIM_SET_MEM_16(data_ptr + 0x188, 0);
    SHIM_SET_MEM_32(data_ptr + 0x190, 0);
  }

  SHIM_SET_RETURN(0);
}


}  // namespace xam
}  // namespace kernel
}  // namespace xe


void xe::kernel::xam::RegisterNetExports(
    ExportResolver* export_resolver, XamState* state) {
  SHIM_SET_MAPPING("xam.xex", NetDll_XNetStartup, state);
  SHIM_SET_MAPPING("xam.xex", NetDll_WSAStartup, state);
}
