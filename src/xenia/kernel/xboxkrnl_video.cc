/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/common.h>
#include <xenia/cpu/cpu.h>
#include <xenia/emulator.h>
#include <xenia/gpu/graphics_system.h>
#include <xenia/gpu/xenos.h>
#include <xenia/kernel/kernel_state.h>
#include <xenia/kernel/util/shim_utils.h>
#include <xenia/kernel/xboxkrnl_private.h>
#include <xenia/kernel/xboxkrnl_rtl.h>
#include <xenia/xbox.h>

namespace xe {
namespace kernel {

using xe::gpu::GraphicsSystem;

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
// http://www.microsoft.com/en-za/download/details.aspx?id=5313 -- "Stripped
// Down Direct3D: Xbox 360 Command Buffer and Resource Management"

SHIM_CALL VdGetCurrentDisplayGamma_shim(PPCContext* ppc_state,
                                        KernelState* state) {
  uint32_t arg0_ptr = SHIM_GET_ARG_32(0);
  uint32_t arg1_ptr = SHIM_GET_ARG_32(1);

  XELOGD("VdGetCurrentDisplayGamma(%.8X, %.8X)", arg0_ptr, arg1_ptr);

  SHIM_SET_MEM_32(arg0_ptr, 2);
  SHIM_SET_MEM_F32(arg1_ptr, 2.22222233f);
}

SHIM_CALL VdGetCurrentDisplayInformation_shim(PPCContext* ppc_state,
                                              KernelState* state) {
  uint32_t ptr = SHIM_GET_ARG_32(0);

  XELOGD("VdGetCurrentDisplayInformation(%.8X)", ptr);

  // Expecting a length 0x58 struct of stuff.
  SHIM_SET_MEM_32(ptr + 0, (1280 << 16) | 720);
  SHIM_SET_MEM_32(ptr + 4, 0);
  SHIM_SET_MEM_32(ptr + 8, 0);
  SHIM_SET_MEM_32(ptr + 12, 0);
  SHIM_SET_MEM_32(ptr + 16, 1280);  // backbuffer width?
  SHIM_SET_MEM_32(ptr + 20, 720);   // backbuffer height?
  SHIM_SET_MEM_32(ptr + 24, 1280);
  SHIM_SET_MEM_32(ptr + 28, 720);
  SHIM_SET_MEM_32(ptr + 32, 1);
  SHIM_SET_MEM_32(ptr + 36, 0);
  SHIM_SET_MEM_32(ptr + 40, 0);
  SHIM_SET_MEM_32(ptr + 44, 0);
  SHIM_SET_MEM_32(ptr + 48, 1);
  SHIM_SET_MEM_32(ptr + 52, 0);
  SHIM_SET_MEM_32(ptr + 56, 0);
  SHIM_SET_MEM_32(ptr + 60, 0);
  SHIM_SET_MEM_32(ptr + 64, 0x014000B4);          // ?
  SHIM_SET_MEM_32(ptr + 68, 0x014000B4);          // ?
  SHIM_SET_MEM_32(ptr + 72, (1280 << 16) | 720);  // actual display size?
  SHIM_SET_MEM_32(ptr + 76, 0x42700000);
  SHIM_SET_MEM_32(ptr + 80, 0);
  SHIM_SET_MEM_32(ptr + 84, 1280);  // display width
}

SHIM_CALL VdQueryVideoFlags_shim(PPCContext* ppc_state, KernelState* state) {
  XELOGD("VdQueryVideoFlags()");

  SHIM_SET_RETURN_64(0x00000006);
}

void xeVdQueryVideoMode(X_VIDEO_MODE* video_mode) {
  if (video_mode == NULL) {
    return;
  }

  // TODO: get info from actual display
  video_mode->display_width = 1280;
  video_mode->display_height = 720;
  video_mode->is_interlaced = 0;
  video_mode->is_widescreen = 1;
  video_mode->is_hi_def = 1;
  video_mode->refresh_rate = 60.0f;
  video_mode->video_standard = 1;  // NTSC
  video_mode->unknown_0x8a = 0x8A;
  video_mode->unknown_0x01 = 0x01;
}

SHIM_CALL VdQueryVideoMode_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t video_mode_ptr = SHIM_GET_ARG_32(0);
  X_VIDEO_MODE* video_mode = (X_VIDEO_MODE*)SHIM_MEM_ADDR(video_mode_ptr);

  XELOGD("VdQueryVideoMode(%.8X)", video_mode_ptr);

  xeVdQueryVideoMode(video_mode);
}

SHIM_CALL VdSetDisplayMode_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t mode = SHIM_GET_ARG_32(0);

  // 40000000
  XELOGD("VdSetDisplayMode(%.8X)", mode);

  SHIM_SET_RETURN_64(0);
}

SHIM_CALL VdSetDisplayModeOverride_shim(PPCContext* ppc_state,
                                        KernelState* state) {
  uint32_t unk0 = SHIM_GET_ARG_32(0);
  uint32_t unk1 = SHIM_GET_ARG_32(1);
  double refresh_rate = ppc_state->f[1];  // 0, 50, 59.9, etc.
  uint32_t unk3 = SHIM_GET_ARG_32(2);
  uint32_t unk4 = SHIM_GET_ARG_32(3);

  // TODO(benvanik): something with refresh rate?
  XELOGD("VdSetDisplayModeOverride(%.8X, %.8X, %g, %.8X, %.8X)", unk0, unk1,
         refresh_rate, unk3, unk4);

  SHIM_SET_RETURN_64(0);
}

SHIM_CALL VdInitializeEngines_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t unk0 = SHIM_GET_ARG_32(0);
  uint32_t callback = SHIM_GET_ARG_32(1);
  uint32_t unk1 = SHIM_GET_ARG_32(2);
  uint32_t unk2_ptr = SHIM_GET_ARG_32(3);
  uint32_t unk3_ptr = SHIM_GET_ARG_32(4);

  XELOGD("VdInitializeEngines(%.8X, %.8X, %.8X, %.8X, %.8X)", unk0, callback,
         unk1, unk2_ptr, unk3_ptr);

  // r3 = 0x4F810000
  // r4 = function ptr (cleanup callback?)
  // r5 = 0
  // r6/r7 = some binary data in .data
}

SHIM_CALL VdShutdownEngines_shim(PPCContext* ppc_state, KernelState* state) {
  XELOGD("VdShutdownEngines()");

  // Ignored for now.
  // Games seem to call an Initialize/Shutdown pair to query info, then
  // re-initialize.
}

SHIM_CALL VdEnableDisableClockGating_shim(PPCContext* ppc_state,
                                          KernelState* state) {
  uint32_t enabled = SHIM_GET_ARG_32(0);

  XELOGD("VdEnableDisableClockGating(%d)", enabled);

  // Ignored, as it really doesn't matter.

  SHIM_SET_RETURN_64(0);
}

SHIM_CALL VdSetGraphicsInterruptCallback_shim(PPCContext* ppc_state,
                                              KernelState* state) {
  uint32_t callback = SHIM_GET_ARG_32(0);
  uint32_t user_data = SHIM_GET_ARG_32(1);

  XELOGD("VdSetGraphicsInterruptCallback(%.8X, %.8X)", callback, user_data);

  GraphicsSystem* gs = state->emulator()->graphics_system();
  if (!gs) {
    return;
  }

  // callback takes 2 params
  // r3 = bool 0/1 - 0 is normal interrupt, 1 is some acquire/lock mumble
  // r4 = user_data (r4 of VdSetGraphicsInterruptCallback)

  gs->SetInterruptCallback(callback, user_data);
}

SHIM_CALL VdInitializeRingBuffer_shim(PPCContext* ppc_state,
                                      KernelState* state) {
  uint32_t ptr = SHIM_GET_ARG_32(0);
  uint32_t page_count = SHIM_GET_ARG_32(1);

  XELOGD("VdInitializeRingBuffer(%.8X, %.8X)", ptr, page_count);

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

SHIM_CALL VdEnableRingBufferRPtrWriteBack_shim(PPCContext* ppc_state,
                                               KernelState* state) {
  uint32_t ptr = SHIM_GET_ARG_32(0);
  uint32_t block_size = SHIM_GET_ARG_32(1);

  XELOGD("VdEnableRingBufferRPtrWriteBack(%.8X, %.8X)", ptr, block_size);

  GraphicsSystem* gs = state->emulator()->graphics_system();
  if (!gs) {
    return;
  }

  // r4 = 6, usually --- <=19
  gs->EnableReadPointerWriteBack(ptr, block_size);

  ptr += 0x20000000;
  // printf("%.8X", ptr);
  // 0x0110343c

  // r3 = 0x2B10(d3d?) + 0x3C

  //((p + 0x3C) & 0x1FFFFFFF) + ((((p + 0x3C) >> 20) + 0x200) & 0x1000)
  // also 0x3C offset into WriteBacks is PrimaryRingBufferReadIndex
  //(1:17:38 AM) Rick: .text:8201B348                 lwz       r11, 0x2B10(r31)
  //(1:17:38 AM) Rick: .text:8201B34C                 addi      r11, r11, 0x3C
  //(1:17:38 AM) Rick: .text:8201B350                 srwi      r10, r11, 20  #
  // r10 = r11 >> 20
  //(1:17:38 AM) Rick: .text:8201B354                 clrlwi    r11, r11, 3   #
  // r11 = r11 & 0x1FFFFFFF
  //(1:17:38 AM) Rick: .text:8201B358                 addi      r10, r10, 0x200
  //(1:17:39 AM) Rick: .text:8201B35C                 rlwinm    r10, r10,
  // 0,19,19 # r10 = r10 & 0x1000
  //(1:17:39 AM) Rick: .text:8201B360                 add       r3, r10, r11
  //(1:17:39 AM) Rick: .text:8201B364                 bl
  // VdEnableRingBufferRPtrWriteBack
  // TODO(benvanik): something?
}

SHIM_CALL VdGetSystemCommandBuffer_shim(PPCContext* ppc_state,
                                        KernelState* state) {
  uint32_t p0_ptr = SHIM_GET_ARG_32(0);
  uint32_t p1_ptr = SHIM_GET_ARG_32(1);

  XELOGD("VdGetSystemCommandBuffer(%.8X, %.8X)", p0_ptr, p1_ptr);

  SHIM_SET_MEM_32(p0_ptr, 0xBEEF0000);
  SHIM_SET_MEM_32(p1_ptr, 0xBEEF0001);
}

SHIM_CALL VdSetSystemCommandBufferGpuIdentifierAddress_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t unk = SHIM_GET_ARG_32(0);

  XELOGD("VdSetSystemCommandBufferGpuIdentifierAddress(%.8X)", unk);

  // r3 = 0x2B10(d3d?) + 8
}

// VdVerifyMEInitCommand
// r3
// r4 = 19
// no op?

SHIM_CALL VdInitializeScalerCommandBuffer_shim(PPCContext* ppc_state,
                                               KernelState* state) {
  uint32_t unk0 = SHIM_GET_ARG_32(0); // 0?
  uint32_t unk1 = SHIM_GET_ARG_32(1); // 0x050002d0 size of ?
  uint32_t unk2 = SHIM_GET_ARG_32(2); // 0?
  uint32_t unk3 = SHIM_GET_ARG_32(3); // 0x050002d0 size of ?
  uint32_t unk4 = SHIM_GET_ARG_32(4); // 0x050002d0 size of ?
  uint32_t unk5 = SHIM_GET_ARG_32(5); // 7?
  uint32_t unk6 = SHIM_GET_ARG_32(6); // 0x2004909c <-- points to zeros?
  uint32_t unk7 = SHIM_GET_ARG_32(7); // 7?
  // arg8 is in stack!
  uint32_t sp = (uint32_t)ppc_state->r[1];
  // Points to the first 80000000h where the memcpy sources from.
  uint32_t dest_ptr = SHIM_MEM_32(sp + 0x64);

  XELOGD(
      "VdInitializeScalerCommandBuffer(%.8X, %.8X, %.8X, %.8X, %.8X, %.8X, "
      "%.8X, %.8X, %.8X)",
      unk0, unk1, unk2, unk3, unk4, unk5, unk6, unk7, dest_ptr);

  // We could fake the commands here, but I'm not sure the game checks for
  // anything but success (non-zero ret).
  // For now, we just fill it with NOPs.
  size_t total_words = 0x1CC / 4;
  uint8_t* p = SHIM_MEM_ADDR(dest_ptr);
  for (size_t i = 0; i < total_words; ++i, p += 4) {
    poly::store_and_swap(p, 0x80000000);
  }

  // returns memcpy size >> 2 for memcpy(...,...,ret << 2)
  SHIM_SET_RETURN_64(total_words >> 2);
}

SHIM_CALL VdCallGraphicsNotificationRoutines_shim(PPCContext* ppc_state,
                                                  KernelState* state) {
  uint32_t unk_1 = SHIM_GET_ARG_32(0);
  uint32_t args_ptr = SHIM_GET_ARG_32(1);

  assert_true(unk_1 == 1);

  uint16_t fb_width = SHIM_MEM_16(args_ptr + 0);
  uint16_t fb_height = SHIM_MEM_16(args_ptr + 2);
  uint16_t bb_width = SHIM_MEM_16(args_ptr + 4);
  uint16_t bb_height = SHIM_MEM_16(args_ptr + 6);

  XELOGD("VdCallGraphicsNotificationRoutines(%d, %.8X(scale %dx%d -> %dx%d))",
         unk_1, args_ptr, bb_width, bb_height, fb_width, fb_height);

  // TODO(benvanik): what does this mean, I forget:
  // callbacks get 0, r3, r4

  SHIM_SET_RETURN_64(0);
}

SHIM_CALL VdIsHSIOTrainingSucceeded_shim(PPCContext* ppc_state,
                                         KernelState* state) {
  XELOGD("VdIsHSIOTrainingSucceeded()");

  // Not really sure what this should be - code does weird stuff here:
  // (cntlzw    r11, r3  / extrwi    r11, r11, 1, 26)
  SHIM_SET_RETURN_64(1);
}

SHIM_CALL VdPersistDisplay_shim(PPCContext* ppc_state, KernelState* state) {
  XELOGD("VdPersistDisplay(?)");

  // ?
  SHIM_SET_RETURN_64(1);
}

SHIM_CALL VdRetrainEDRAMWorker_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t unk0 = SHIM_GET_ARG_32(0);

  XELOGD("VdRetrainEDRAMWorker(%.8X)", unk0);

  SHIM_SET_RETURN_64(0);
}

SHIM_CALL VdRetrainEDRAM_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t unk0 = SHIM_GET_ARG_32(0);
  uint32_t unk1 = SHIM_GET_ARG_32(1);
  uint32_t unk2 = SHIM_GET_ARG_32(2);
  uint32_t unk3 = SHIM_GET_ARG_32(3);
  uint32_t unk4 = SHIM_GET_ARG_32(4);
  uint32_t unk5 = SHIM_GET_ARG_32(5);

  XELOGD("VdRetrainEDRAM(%.8X, %.8X, %.8X, %.8X, %.8X, %.8X)", unk0, unk1, unk2,
         unk3, unk4, unk5);

  SHIM_SET_RETURN_64(0);
}

SHIM_CALL VdSwap_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t unk0 = SHIM_GET_ARG_32(0);  // ptr into primary ringbuffer
  uint32_t unk1 = SHIM_GET_ARG_32(1);
  uint32_t unk2 = SHIM_GET_ARG_32(2);
  uint32_t unk3 = SHIM_GET_ARG_32(3);             // ptr to 0xBEEF0000
  uint32_t unk4 = SHIM_GET_ARG_32(4);             // 0xBEEF0001
  uint32_t frontbuffer_ptr = SHIM_GET_ARG_32(5);  // ptr to frontbuffer address
  uint32_t unk6 = SHIM_GET_ARG_32(6);             // ptr to 6?
  uint32_t unk7 = SHIM_GET_ARG_32(7);

  uint32_t frontbuffer = SHIM_MEM_32(frontbuffer_ptr);

  XELOGD("VdSwap(%.8X, %.8X, %.8X, %.8X, %.8X, %.8X(%.8X), %.8X, %.8X)", unk0,
         unk1, unk2, unk3, unk4, frontbuffer_ptr, frontbuffer, unk6, unk7);

  // The caller seems to reserve 64 words (256b) in the primary ringbuffer
  // for this method to do what it needs. We just zero them out and send a
  // token value. It'd be nice to figure out what this is really doing so
  // that we could simulate it, though due to TCR I bet all games need to
  // use this method.
  memset(SHIM_MEM_ADDR(unk0), 0, 64 * 4);
  auto dwords = reinterpret_cast<uint32_t*>(SHIM_MEM_ADDR(unk0));
  dwords[0] = poly::byte_swap((0x03 << 30) | ((63 - 1) << 16) |
                              (xe::gpu::xenos::PM4_XE_SWAP << 8));
  dwords[1] = poly::byte_swap(frontbuffer);

  SHIM_SET_RETURN_64(0);
}

}  // namespace kernel
}  // namespace xe

void xe::kernel::xboxkrnl::RegisterVideoExports(ExportResolver* export_resolver,
                                                KernelState* state) {
  SHIM_SET_MAPPING("xboxkrnl.exe", VdGetCurrentDisplayGamma, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", VdGetCurrentDisplayInformation, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", VdQueryVideoFlags, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", VdQueryVideoMode, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", VdSetDisplayMode, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", VdSetDisplayModeOverride, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", VdInitializeEngines, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", VdShutdownEngines, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", VdEnableDisableClockGating, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", VdSetGraphicsInterruptCallback, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", VdInitializeRingBuffer, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", VdEnableRingBufferRPtrWriteBack, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", VdGetSystemCommandBuffer, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", VdSetSystemCommandBufferGpuIdentifierAddress,
                   state);
  SHIM_SET_MAPPING("xboxkrnl.exe", VdInitializeScalerCommandBuffer, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", VdCallGraphicsNotificationRoutines, state);
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
  export_resolver->SetVariableMapping("xboxkrnl.exe", ordinals::VdGlobalDevice,
                                      pVdGlobalDevice);
  poly::store_and_swap<uint32_t>(mem + pVdGlobalDevice, 0);

  // VdGlobalXamDevice (4b)
  // Pointer to the XAM D3D device, which we don't have.
  uint32_t pVdGlobalXamDevice = (uint32_t)memory->HeapAlloc(0, 4, 0);
  export_resolver->SetVariableMapping(
      "xboxkrnl.exe", ordinals::VdGlobalXamDevice, pVdGlobalXamDevice);
  poly::store_and_swap<uint32_t>(mem + pVdGlobalXamDevice, 0);

  // VdGpuClockInMHz (4b)
  // GPU clock. Xenos is 500MHz. Hope nothing is relying on this timing...
  uint32_t pVdGpuClockInMHz = (uint32_t)memory->HeapAlloc(0, 4, 0);
  export_resolver->SetVariableMapping("xboxkrnl.exe", ordinals::VdGpuClockInMHz,
                                      pVdGpuClockInMHz);
  poly::store_and_swap<uint32_t>(mem + pVdGpuClockInMHz, 500);

  // VdHSIOCalibrationLock (28b)
  // CriticalSection.
  uint32_t pVdHSIOCalibrationLock = (uint32_t)memory->HeapAlloc(0, 28, 0);
  export_resolver->SetVariableMapping(
      "xboxkrnl.exe", ordinals::VdHSIOCalibrationLock, pVdHSIOCalibrationLock);
  auto hsio_lock =
      reinterpret_cast<X_RTL_CRITICAL_SECTION*>(mem + pVdHSIOCalibrationLock);
  xeRtlInitializeCriticalSectionAndSpinCount(hsio_lock, 10000);
}
