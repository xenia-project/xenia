/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/xam/xam_info.h>

#include <xenia/kernel/shim_utils.h>
#include <xenia/kernel/xex2.h>
#include <xenia/kernel/xam/xam_private.h>
#include <xenia/kernel/xam/xam_state.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xam;


namespace xe {
namespace kernel {
namespace xam {


SHIM_CALL XGetAVPack_shim(
    PPCContext* ppc_state, XamState* state) {
  // DWORD
  // Not sure what the values are for this, but 6 is VGA.
  // Other likely values are 3/4/8 for HDMI or something.
  // Games seem to use this as a PAL check - if the result is not 3/4/6/8
  // they explode with errors if not in PAL mode.
  SHIM_SET_RETURN(6);
}


SHIM_CALL XGetGameRegion_shim(
    PPCContext* ppc_state, XamState* state) {
  XELOGD("XGetGameRegion()");

  SHIM_SET_RETURN(XEX_REGION_ALL);
}


SHIM_CALL XGetLanguage_shim(
    PPCContext* ppc_state, XamState* state) {
  XELOGD("XGetLanguage()");

  uint32_t desired_language = X_LANGUAGE_ENGLISH;

  // Switch the language based on game region.
  // TODO(benvanik): pull from xex header.
  uint32_t game_region = XEX_REGION_NTSCU;
  if (game_region & XEX_REGION_NTSCU) {
    desired_language = X_LANGUAGE_ENGLISH;
  } else if (game_region & XEX_REGION_NTSCJ) {
    desired_language = X_LANGUAGE_JAPANESE;
  }
  // Add more overrides?

  SHIM_SET_RETURN(desired_language);
}


}  // namespace xam
}  // namespace kernel
}  // namespace xe


void xe::kernel::xam::RegisterInfoExports(
    ExportResolver* export_resolver, XamState* state) {
  SHIM_SET_MAPPING("xam.xex", XGetAVPack, state);
  SHIM_SET_MAPPING("xam.xex", XGetGameRegion, state);
  SHIM_SET_MAPPING("xam.xex", XGetLanguage, state);
}
