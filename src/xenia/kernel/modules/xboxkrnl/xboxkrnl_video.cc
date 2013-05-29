/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/modules/xboxkrnl/xboxkrnl_video.h>

#include <xenia/kernel/shim_utils.h>
#include <xenia/kernel/modules/xboxkrnl/kernel_state.h>
#include <xenia/kernel/modules/xboxkrnl/xboxkrnl_private.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;


namespace xe {
namespace kernel {
namespace xboxkrnl {


// http://www.tweakoz.com/orkid/
// http://www.tweakoz.com/orkid/dox/d3/d52/xb360init_8cpp_source.html
// https://github.com/Free60Project/xenosfb/
// https://github.com/Free60Project/xenosfb/blob/master/src/xe.h
// https://github.com/gligli/libxemit
// http://web.archive.org/web/20090428095215/http://msdn.microsoft.com/en-us/library/bb313877.aspx
// http://web.archive.org/web/20100423054747/http://msdn.microsoft.com/en-us/library/bb313961.aspx
// http://web.archive.org/web/20100423054747/http://msdn.microsoft.com/en-us/library/bb313878.aspx
// http://web.archive.org/web/20090510235238/http://msdn.microsoft.com/en-us/library/bb313942.aspx
// http://svn.dd-wrt.com/browser/src/linux/universal/linux-3.8/drivers/gpu/drm/radeon/radeon_ring.c
// http://www.microsoft.com/en-za/download/details.aspx?id=5313 -- "Stripped Down Direct3D: Xbox 360 Command Buffer and Resource Management"


// VdQueryVideoFlags
// return 0x7?


void xeVdQueryVideoMode(X_VIDEO_MODE *video_mode, bool swap) {
  if (video_mode == NULL) {
    return;
  }

  // TODO: get info from actual display
  video_mode->display_width  = 1280;
  video_mode->display_height = 720;
  video_mode->is_interlaced  = 0;
  video_mode->is_widescreen  = 1;
  video_mode->is_hi_def      = 1;
  video_mode->refresh_rate   = 60.0f;
  video_mode->video_standard = 1; // NTSC

  if (swap) {
    video_mode->display_width  = XESWAP32BE(video_mode->display_width);
    video_mode->display_height = XESWAP32BE(video_mode->display_height);
    video_mode->is_interlaced  = XESWAP32BE(video_mode->is_interlaced);
    video_mode->is_widescreen  = XESWAP32BE(video_mode->is_widescreen);
    video_mode->is_hi_def      = XESWAP32BE(video_mode->is_hi_def);
    video_mode->refresh_rate   = XESWAPF32BE(video_mode->refresh_rate);
    video_mode->video_standard = XESWAP32BE(video_mode->video_standard);
  }
}


SHIM_CALL VdQueryVideoMode_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  uint32_t video_mode_ptr = SHIM_GET_ARG_32(0);
  X_VIDEO_MODE *video_mode = (X_VIDEO_MODE*)SHIM_MEM_ADDR(video_mode_ptr);

  XELOGD(
      "VdQueryVideoMode(%.8X)",
      video_mode_ptr);

  xeVdQueryVideoMode(video_mode, true);
}


void xeVdInitializeEngines(uint32_t unk0, uint32_t callback, uint32_t unk1,
                           uint32_t unk2_ptr, uint32_t unk3_ptr) {
  // r3 = 0x4F810000
  // r4 = function ptr (ready callback?)
  // r5 = 0
  // r6/r7 = some binary data in .data
}


SHIM_CALL VdInitializeEngines_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  uint32_t unk0 = SHIM_GET_ARG_32(0);
  uint32_t callback = SHIM_GET_ARG_32(1);
  uint32_t unk1 = SHIM_GET_ARG_32(2);
  uint32_t unk2_ptr = SHIM_GET_ARG_32(3);
  uint32_t unk3_ptr = SHIM_GET_ARG_32(4);

  XELOGD(
      "VdInitializeEngines(%.8X, %.8X, %.8X, %.8X, %.8X)",
      unk0, callback, unk1, unk2_ptr, unk3_ptr);

  xeVdInitializeEngines(unk0, callback, unk1, unk2_ptr, unk3_ptr);
}


void xeVdSetGraphicsInterruptCallback(uint32_t callback, uint32_t user_data) {
  // callback takes 2 params
  // r3 = bool 0/1 - 0 is normal interrupt, 1 is some acquire/lock mumble
  // r4 = user_data (r4 of VdSetGraphicsInterruptCallback)
}


SHIM_CALL VdSetGraphicsInterruptCallback_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  uint32_t callback = SHIM_GET_ARG_32(0);
  uint32_t user_data = SHIM_GET_ARG_32(1);

  XELOGD(
      "VdSetGraphicsInterruptCallback(%.8X, %.8X)",
      callback, user_data);

  xeVdSetGraphicsInterruptCallback(callback, user_data);
}


// VdInitializeRingBuffer
// r3 = result of MmGetPhysicalAddress
// r4 = number of pages? page size?
//      0x8000 -> cntlzw=16 -> 0x1C - 16 = 12
// ring_buffer_t {
//   uint 0
//   uint buffer_0_size
//   uint buffer_0_ptr
//   uint buffer_1_size
//   uint buffer_1_ptr
//   uint segment_count   -- 32 common
// }
// Buffer pointers are from MmAllocatePhysicalMemory with WRITE_COMBINE.
// Sizes could be zero? XBLA games seem to do this. Default sizes?
// D3D does size / region_count - must be > 1024


// void VdEnableRingBufferRPtrWriteBack
// r3 = ? pointer? doesn't look like a valid one
// r4 = 6, usually --- <=19
// Same value used to calculate the pointer is later written to
// Maybe GPU-memory relative?


// VdSetSystemCommandBufferGpuIdentifierAddress
// r3 = ?


// VdVerifyMEInitCommand
// r3
// r4 = 19
// no op?


}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe


void xe::kernel::xboxkrnl::RegisterVideoExports(
    ExportResolver* export_resolver, KernelState* state) {
  SHIM_SET_MAPPING("xboxkrnl.exe", VdQueryVideoMode, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", VdInitializeEngines, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", VdSetGraphicsInterruptCallback, state);
}
