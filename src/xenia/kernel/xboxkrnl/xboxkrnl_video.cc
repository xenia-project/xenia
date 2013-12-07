/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/xboxkrnl/xboxkrnl_video.h>

#include <xenia/emulator.h>
#include <xenia/cpu/cpu.h>
#include <xenia/gpu/gpu.h>
#include <xenia/kernel/shim_utils.h>
#include <xenia/kernel/xboxkrnl/kernel_state.h>
#include <xenia/kernel/xboxkrnl/xboxkrnl_private.h>
#include <xenia/kernel/xboxkrnl/xboxkrnl_rtl.h>


using namespace xe;
using namespace xe::gpu;
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


void xeVdGetCurrentDisplayGamma(uint32_t* arg0, float* arg1) {
  *arg0 = 2;
  *arg1 = 2.22222233f;
}


SHIM_CALL VdGetCurrentDisplayGamma_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t arg0_ptr = SHIM_GET_ARG_32(0);
  uint32_t arg1_ptr = SHIM_GET_ARG_32(1);

  XELOGD(
      "VdGetCurrentDisplayGamma(%.8X, %.8X)",
      arg0_ptr, arg1_ptr);

  uint32_t arg0 = 0;
  union {
    float float_value;
    uint32_t uint_value;
  } arg1;
  xeVdGetCurrentDisplayGamma(&arg0, &arg1.float_value);
  SHIM_SET_MEM_32(arg0_ptr, arg0);
  SHIM_SET_MEM_32(arg1_ptr, arg1.uint_value);
}


SHIM_CALL VdGetCurrentDisplayInformation_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t ptr = SHIM_GET_ARG_32(0);

  XELOGD(
      "VdGetCurrentDisplayInformation(%.8X)",
      ptr);

  // Expecting a length 0x58 struct of stuff.
  SHIM_SET_MEM_32(ptr + 0x10, 1280);
  SHIM_SET_MEM_32(ptr + 0x14, 720);
  SHIM_SET_MEM_16(ptr + 0x48, 1280);
  SHIM_SET_MEM_16(ptr + 0x4A, 720);
  SHIM_SET_MEM_16(ptr + 0x56, 1280);
}


uint32_t xeVdQueryVideoFlags() {
  // ?
  return 0x00000007;
}


SHIM_CALL VdQueryVideoFlags_shim(
    PPCContext* ppc_state, KernelState* state) {
  XELOGD(
      "VdQueryVideoFlags()");

  SHIM_SET_RETURN(xeVdQueryVideoFlags());
}


void xeVdQueryVideoMode(X_VIDEO_MODE *video_mode, bool swap) {
  if (video_mode == NULL) {
    return;
  }

  // TODO: get info from actual display
  video_mode->display_width   = 1280;
  video_mode->display_height  = 720;
  video_mode->is_interlaced   = 0;
  video_mode->is_widescreen   = 1;
  video_mode->is_hi_def       = 1;
  video_mode->refresh_rate    = 60.0f;
  video_mode->video_standard  = 1; // NTSC
  video_mode->unknown_0x8a    = 0x8A;
  video_mode->unknown_0x01    = 0x01;

  if (swap) {
    video_mode->display_width   = XESWAP32BE(video_mode->display_width);
    video_mode->display_height  = XESWAP32BE(video_mode->display_height);
    video_mode->is_interlaced   = XESWAP32BE(video_mode->is_interlaced);
    video_mode->is_widescreen   = XESWAP32BE(video_mode->is_widescreen);
    video_mode->is_hi_def       = XESWAP32BE(video_mode->is_hi_def);
    video_mode->refresh_rate    = XESWAPF32BE(video_mode->refresh_rate);
    video_mode->video_standard  = XESWAP32BE(video_mode->video_standard);
    video_mode->unknown_0x8a    = XESWAP32BE(video_mode->unknown_0x8a);
    video_mode->unknown_0x01    = XESWAP32BE(video_mode->unknown_0x01);
  }
}


SHIM_CALL VdQueryVideoMode_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t video_mode_ptr = SHIM_GET_ARG_32(0);
  X_VIDEO_MODE *video_mode = (X_VIDEO_MODE*)SHIM_MEM_ADDR(video_mode_ptr);

  XELOGD(
      "VdQueryVideoMode(%.8X)",
      video_mode_ptr);

  xeVdQueryVideoMode(video_mode, true);
}


void xeVdInitializeEngines(uint32_t unk0, uint32_t callback, uint32_t unk1,
                           uint32_t unk2_ptr, uint32_t unk3_ptr) {
  KernelState* state = shared_kernel_state_;
  XEASSERTNOTNULL(state);
  GraphicsSystem* gs = state->emulator()->graphics_system();
  if (!gs) {
    return;
  }

  // r3 = 0x4F810000
  // r4 = function ptr (cleanup callback?)
  // r5 = 0
  // r6/r7 = some binary data in .data
}


SHIM_CALL VdInitializeEngines_shim(
    PPCContext* ppc_state, KernelState* state) {
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
  KernelState* state = shared_kernel_state_;
  XEASSERTNOTNULL(state);
  GraphicsSystem* gs = state->emulator()->graphics_system();
  if (!gs) {
    return;
  }

  // callback takes 2 params
  // r3 = bool 0/1 - 0 is normal interrupt, 1 is some acquire/lock mumble
  // r4 = user_data (r4 of VdSetGraphicsInterruptCallback)

  gs->SetInterruptCallback(callback, user_data);
}


SHIM_CALL VdSetGraphicsInterruptCallback_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t callback = SHIM_GET_ARG_32(0);
  uint32_t user_data = SHIM_GET_ARG_32(1);

  XELOGD(
      "VdSetGraphicsInterruptCallback(%.8X, %.8X)",
      callback, user_data);

  xeVdSetGraphicsInterruptCallback(callback, user_data);
}


void xeVdInitializeRingBuffer(uint32_t ptr, uint32_t page_count) {
  KernelState* state = shared_kernel_state_;
  XEASSERTNOTNULL(state);
  GraphicsSystem* gs = state->emulator()->graphics_system();
  if (!gs) {
    return;
  }

  // r3 = result of MmGetPhysicalAddress
  // r4 = number of pages? page size?
  //      0x8000 -> cntlzw=16 -> 0x1C - 16 = 12
  // Buffer pointers are from MmAllocatePhysicalMemory with WRITE_COMBINE.
  // Sizes could be zero? XBLA games seem to do this. Default sizes?
  // D3D does size / region_count - must be > 1024

  gs->InitializeRingBuffer(ptr, page_count);
}


SHIM_CALL VdInitializeRingBuffer_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t ptr = SHIM_GET_ARG_32(0);
  uint32_t page_count = SHIM_GET_ARG_32(1);

  XELOGD(
      "VdInitializeRingBuffer(%.8X, %.8X)",
      ptr, page_count);

  xeVdInitializeRingBuffer(ptr, page_count);
}


void xeVdEnableRingBufferRPtrWriteBack(uint32_t ptr, uint32_t block_size) {
  KernelState* state = shared_kernel_state_;
  XEASSERTNOTNULL(state);
  GraphicsSystem* gs = state->emulator()->graphics_system();
  if (!gs) {
    return;
  }

  // r4 = 6, usually --- <=19
  gs->EnableReadPointerWriteBack(ptr, block_size);

  ptr += 0x20000000;
  printf("%.8X", ptr);
  // 0x0110343c

  // r3 = 0x2B10(d3d?) + 0x3C

  //((p + 0x3C) & 0x1FFFFFFF) + ((((p + 0x3C) >> 20) + 0x200) & 0x1000)
  //also 0x3C offset into WriteBacks is PrimaryRingBufferReadIndex
//(1:17:38 AM) Rick: .text:8201B348                 lwz       r11, 0x2B10(r31)
//(1:17:38 AM) Rick: .text:8201B34C                 addi      r11, r11, 0x3C
//(1:17:38 AM) Rick: .text:8201B350                 srwi      r10, r11, 20  # r10 = r11 >> 20
//(1:17:38 AM) Rick: .text:8201B354                 clrlwi    r11, r11, 3   # r11 = r11 & 0x1FFFFFFF
//(1:17:38 AM) Rick: .text:8201B358                 addi      r10, r10, 0x200
//(1:17:39 AM) Rick: .text:8201B35C                 rlwinm    r10, r10, 0,19,19 # r10 = r10 & 0x1000
//(1:17:39 AM) Rick: .text:8201B360                 add       r3, r10, r11
//(1:17:39 AM) Rick: .text:8201B364                 bl        VdEnableRingBufferRPtrWriteBack
  // TODO(benvanik): something?
}


SHIM_CALL VdEnableRingBufferRPtrWriteBack_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t ptr = SHIM_GET_ARG_32(0);
  uint32_t block_size = SHIM_GET_ARG_32(1);

  XELOGD(
      "VdEnableRingBufferRPtrWriteBack(%.8X, %.8X)",
      ptr, block_size);

  xeVdEnableRingBufferRPtrWriteBack(ptr, block_size);
}


void xeVdGetSystemCommandBuffer(uint32_t* p0, uint32_t* p1) {
  *p0 = 0xBEEF0000;
  *p1 = 0xBEEF0001;
}


SHIM_CALL VdGetSystemCommandBuffer_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t p0_ptr = SHIM_GET_ARG_32(0);
  uint32_t p1_ptr = SHIM_GET_ARG_32(1);

  XELOGD(
    "VdGetSystemCommandBuffer(%.8X, %.8X)",
    p0_ptr,
    p1_ptr);

  uint32_t p0 = 0;
  uint32_t p1 = 0;
  xeVdGetSystemCommandBuffer(&p0, &p1);
  SHIM_SET_MEM_32(p0_ptr, p0);
  SHIM_SET_MEM_32(p1_ptr, p1);
}


void xeVdSetSystemCommandBufferGpuIdentifierAddress(uint32_t unk) {
  KernelState* state = shared_kernel_state_;
  XEASSERTNOTNULL(state);
  GraphicsSystem* gs = state->emulator()->graphics_system();
  if (!gs) {
    return;
  }

  // r3 = 0x2B10(d3d?) + 8
}


SHIM_CALL VdSetSystemCommandBufferGpuIdentifierAddress_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t unk = SHIM_GET_ARG_32(0);

  XELOGD(
      "VdSetSystemCommandBufferGpuIdentifierAddress(%.8X)",
      unk);

  xeVdSetSystemCommandBufferGpuIdentifierAddress(unk);
}


// VdVerifyMEInitCommand
// r3
// r4 = 19
// no op?


// VdCallGraphicsNotificationRoutines
// r3 = 1
// r4 = ?
// callbacks get 0, r3, r4


SHIM_CALL VdIsHSIOTrainingSucceeded_shim(
    PPCContext* ppc_state, KernelState* state) {
  XELOGD(
      "VdIsHSIOTrainingSucceeded()");

  // Not really sure what this should be - code does weird stuff here:
  // (cntlzw    r11, r3  / extrwi    r11, r11, 1, 26)
  SHIM_SET_RETURN(1);
}


SHIM_CALL VdPersistDisplay_shim(
    PPCContext* ppc_state, KernelState* state) {
  XELOGD(
      "VdPersistDisplay(?)");

  // ?
  SHIM_SET_RETURN(1);
}


SHIM_CALL VdRetrainEDRAMWorker_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t unk0 = SHIM_GET_ARG_32(0);

  XELOGD(
      "VdRetrainEDRAMWorker(%.8X)",
      unk0);

  SHIM_SET_RETURN(0);
}


SHIM_CALL VdRetrainEDRAM_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t unk0 = SHIM_GET_ARG_32(0);
  uint32_t unk1 = SHIM_GET_ARG_32(1);
  uint32_t unk2 = SHIM_GET_ARG_32(2);
  uint32_t unk3 = SHIM_GET_ARG_32(3);
  uint32_t unk4 = SHIM_GET_ARG_32(4);
  uint32_t unk5 = SHIM_GET_ARG_32(5);

  XELOGD(
      "VdRetrainEDRAM(%.8X, %.8X, %.8X, %.8X, %.8X, %.8X)",
      unk0, unk1, unk2, unk3, unk4, unk5);

  SHIM_SET_RETURN(0);
}


SHIM_CALL VdSwap_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t unk0 = SHIM_GET_ARG_32(0); // ptr into primary ringbuffer
  uint32_t unk1 = SHIM_GET_ARG_32(1);
  uint32_t unk2 = SHIM_GET_ARG_32(2);
  uint32_t unk3 = SHIM_GET_ARG_32(3); // ptr to 0xBEEF0000
  uint32_t unk4 = SHIM_GET_ARG_32(4); // 0xBEEF0001
  uint32_t unk5 = SHIM_GET_ARG_32(5);
  uint32_t unk6 = SHIM_GET_ARG_32(6);
  uint32_t unk7 = SHIM_GET_ARG_32(7);

  XELOGD(
      "VdSwap(%.8X, %.8X, %.8X, %.8X, %.8X, %.8X, %.8X, %.8X)",
      unk0,
      unk1,
      unk2,
      unk3,
      unk4,
      unk5,
      unk6,
      unk7);

  KernelState* kernel_state = shared_kernel_state_;
  XEASSERTNOTNULL(kernel_state);
  GraphicsSystem* gs = kernel_state->emulator()->graphics_system();
  if (!gs) {
    return;
  }

  gs->set_swap_pending(true);

  // The caller seems to reserve 64 words (256b) in the primary ringbuffer
  // for this method to do what it needs. We just zero them out. We could
  // encode the parameters in the stream for the ringbuffer, if needed.
  xe_zero_struct(SHIM_MEM_ADDR(unk0), 64 * 4);

  SHIM_SET_RETURN(0);
}


}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe


void xe::kernel::xboxkrnl::RegisterVideoExports(
    ExportResolver* export_resolver, KernelState* state) {
  SHIM_SET_MAPPING("xboxkrnl.exe", VdGetCurrentDisplayGamma, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", VdGetCurrentDisplayInformation, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", VdQueryVideoFlags, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", VdQueryVideoMode, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", VdInitializeEngines, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", VdSetGraphicsInterruptCallback, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", VdInitializeRingBuffer, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", VdEnableRingBufferRPtrWriteBack, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", VdGetSystemCommandBuffer, state);
  SHIM_SET_MAPPING("xboxkrnl.exe",
      VdSetSystemCommandBufferGpuIdentifierAddress, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", VdIsHSIOTrainingSucceeded, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", VdPersistDisplay, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", VdRetrainEDRAMWorker, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", VdRetrainEDRAM, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", VdSwap, state);

  Memory* memory = state->memory();
  uint8_t* mem = memory->membase();

  // VdGlobalDevice (4b)
  // Pointer to a global D3D device. Games only seem to set this, so we don't
  // have to do anything. We may want to read it back later, though.
  uint32_t pVdGlobalDevice = (uint32_t)memory->HeapAlloc(0, 4, 0);
  export_resolver->SetVariableMapping(
      "xboxkrnl.exe", ordinals::VdGlobalDevice,
      pVdGlobalDevice);
  XESETUINT32BE(mem + pVdGlobalDevice, 0);

  // VdGlobalXamDevice (4b)
  // Pointer to the XAM D3D device, which we don't have.
  uint32_t pVdGlobalXamDevice = (uint32_t)memory->HeapAlloc(0, 4, 0);
  export_resolver->SetVariableMapping(
      "xboxkrnl.exe", ordinals::VdGlobalXamDevice,
      pVdGlobalXamDevice);
  XESETUINT32BE(mem + pVdGlobalXamDevice, 0);

  // VdGpuClockInMHz (4b)
  // GPU clock. Xenos is 500MHz. Hope nothing is relying on this timing...
  uint32_t pVdGpuClockInMHz = (uint32_t)memory->HeapAlloc(0, 4, 0);
  export_resolver->SetVariableMapping(
      "xboxkrnl.exe", ordinals::VdGpuClockInMHz,
      pVdGpuClockInMHz);
  XESETUINT32BE(mem + pVdGpuClockInMHz, 500);

  // VdHSIOCalibrationLock (28b)
  // CriticalSection.
  uint32_t pVdHSIOCalibrationLock = (uint32_t)memory->HeapAlloc(0, 28, 0);
  export_resolver->SetVariableMapping(
      "xboxkrnl.exe", ordinals::VdHSIOCalibrationLock,
      pVdHSIOCalibrationLock);
  xeRtlInitializeCriticalSectionAndSpinCount(pVdHSIOCalibrationLock, 10000);
}
