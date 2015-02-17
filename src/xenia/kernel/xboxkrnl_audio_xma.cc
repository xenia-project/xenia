/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/apu/apu.h"
#include "xenia/common.h"
#include "xenia/emulator.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xboxkrnl_private.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {

// See audio_system.cc for implementation details.
//
// XMA details:
// https://devel.nuclex.org/external/svn/directx/trunk/include/xma2defs.h
// https://github.com/gdawg/fsbext/blob/master/src/xma_header.h
//
// XMA is undocumented, but the methods are pretty simple.
// Games do this sequence to decode (now):
//   (not sure we are setting buffer validity/offsets right)
// d> XMACreateContext(20656800)
// d> XMAIsInputBuffer0Valid(000103E0)
// d> XMAIsInputBuffer1Valid(000103E0)
// d> XMADisableContext(000103E0, 0)
// d> XMABlockWhileInUse(000103E0)
// d> XMAInitializeContext(000103E0, 20008810)
// d> XMASetOutputBufferValid(000103E0)
// d> XMASetInputBuffer0Valid(000103E0)
// d> XMAEnableContext(000103E0)
// d> XMAGetOutputBufferWriteOffset(000103E0)
// d> XMAGetOutputBufferReadOffset(000103E0)
// d> XMAIsOutputBufferValid(000103E0)
// d> XMAGetOutputBufferReadOffset(000103E0)
// d> XMAGetOutputBufferWriteOffset(000103E0)
// d> XMAIsInputBuffer0Valid(000103E0)
// d> XMAIsInputBuffer1Valid(000103E0)
// d> XMAIsInputBuffer0Valid(000103E0)
// d> XMAIsInputBuffer1Valid(000103E0)
// d> XMAReleaseContext(000103E0)
//
// XAudio2 uses XMA under the covers, and seems to map with the same
// restrictions of frame/subframe/etc:
// https://msdn.microsoft.com/en-us/library/windows/desktop/microsoft.directx_sdk.xaudio2.xaudio2_buffer(v=vs.85).aspx
//
//
//static const uint32_t kBytesPerBlock = 2048;
//static const uint32_t kSamplesPerFrame = 512;
//static const uint32_t kSamplesPerSubframe = 128;
//
//// This is unused; just for documentation of reversing.
//struct XMAContextType {
//  // DWORD 0
//  uint32_t input_buffer_0_block_count : 12;  // XMASetInputBuffer0, number of
//                                             // 2KB blocks.
//  uint32_t loop_count : 8;                   // +12bit, XMASetLoopData
//  uint32_t input_buffer_0_valid : 1;         // +20bit, XMAIsInputBuffer0Valid
//  uint32_t input_buffer_1_valid : 1;         // +21bit, XMAIsInputBuffer1Valid
//  uint32_t output_buffer_block_count : 5;    // +22bit
//  uint32_t
//      output_buffer_write_offset : 5;  // +27bit, XMAGetOutputBufferWriteOffset
//
//  // DWORD 1
//  uint32_t input_buffer_1_block_count : 12;  // XMASetInputBuffer1, number of
//                                             // 2KB blocks.
//  uint32_t loop_subframe_end : 2;            // +12bit, XMASetLoopData
//  uint32_t unk_dword_1_a : 3;                // ?
//  uint32_t loop_subframe_skip : 3;           // +17bit, XMASetLoopData
//  uint32_t subframe_decode_count : 4;        // +20bit
//  uint32_t unk_dword_1_b : 3;                // ?
//  uint32_t sample_rate : 2;                  // +27bit
//  uint32_t is_stereo : 1;                    // +29bit
//  uint32_t unk_dword_1_c : 1;                // ?
//  uint32_t output_buffer_valid : 1;          // +31bit, XMAIsOutputBufferValid
//
//  // DWORD 2
//  uint32_t input_buffer_read_offset : 30;  // XMAGetInputBufferReadOffset
//  uint32_t unk_dword_2 : 2;                // ?
//
//  // DWORD 3
//  uint32_t loop_start : 26;      // XMASetLoopData
//  uint32_t unk_dword_3 : 6;  // ?
//
//  // DWORD 4
//  uint32_t loop_end : 26;    // XMASetLoopData
//  uint32_t packet_metadata : 5;  // XMAGetPacketMetadata
//  uint32_t current_buffer : 1;   // ?
//
//  // DWORD 5
//  uint32_t input_buffer_0_ptr;  // top bits lopped off?
//  // DWORD 6
//  uint32_t input_buffer_1_ptr;  // top bits lopped off?
//  // DWORD 7
//  uint32_t output_buffer_ptr;  // top bits lopped off?
//  // DWORD 8
//  uint32_t unk_dword_8;  // Some kind of pointer like output_buffer_ptr
//
//  // DWORD 9
//  uint32_t
//      output_buffer_read_offset : 5;  // +0bit, XMAGetOutputBufferReadOffset
//  uint32_t unk_dword_9 : 27;
//};

SHIM_CALL XMACreateContext_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t context_out_ptr = SHIM_GET_ARG_32(0);

  XELOGD("XMACreateContext(%.8X)", context_out_ptr);

  auto audio_system = state->emulator()->audio_system();
  uint32_t context_ptr = audio_system->AllocateXmaContext();
  SHIM_SET_MEM_32(context_out_ptr, context_ptr);
  if (!context_ptr) {
    SHIM_SET_RETURN_32(X_STATUS_NO_MEMORY);
    return;
  }

  SHIM_SET_RETURN_32(X_STATUS_SUCCESS);
}

SHIM_CALL XMAReleaseContext_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t context_ptr = SHIM_GET_ARG_32(0);

  XELOGD("XMAReleaseContext(%.8X)", context_ptr);

  auto audio_system = state->emulator()->audio_system();
  audio_system->ReleaseXmaContext(context_ptr);

  SHIM_SET_RETURN_32(0);
}

}  // namespace kernel
}  // namespace xe

void xe::kernel::xboxkrnl::RegisterAudioXmaExports(ExportResolver* export_resolver,
                                                KernelState* state) {
  SHIM_SET_MAPPING("xboxkrnl.exe", XMACreateContext, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", XMAReleaseContext, state);

  //SHIM_SET_MAPPING("xboxkrnl.exe", XMAInitializeContext, state);
  //SHIM_SET_MAPPING("xboxkrnl.exe", XMASetLoopData, state);
  //SHIM_SET_MAPPING("xboxkrnl.exe", XMAGetInputBufferReadOffset, state);
  //SHIM_SET_MAPPING("xboxkrnl.exe", XMASetInputBufferReadOffset, state);
  //SHIM_SET_MAPPING("xboxkrnl.exe", XMASetInputBuffer0, state);
  //SHIM_SET_MAPPING("xboxkrnl.exe", XMAIsInputBuffer0Valid, state);
  //SHIM_SET_MAPPING("xboxkrnl.exe", XMASetInputBuffer0Valid, state);
  //SHIM_SET_MAPPING("xboxkrnl.exe", XMASetInputBuffer1, state);
  //SHIM_SET_MAPPING("xboxkrnl.exe", XMAIsInputBuffer1Valid, state);
  //SHIM_SET_MAPPING("xboxkrnl.exe", XMASetInputBuffer1Valid, state);
  //SHIM_SET_MAPPING("xboxkrnl.exe", XMAIsOutputBufferValid, state);
  //SHIM_SET_MAPPING("xboxkrnl.exe", XMASetOutputBufferValid, state);
  //SHIM_SET_MAPPING("xboxkrnl.exe", XMASetOutputBufferReadOffset, state);
  //SHIM_SET_MAPPING("xboxkrnl.exe", XMAGetOutputBufferReadOffset, state);
  //SHIM_SET_MAPPING("xboxkrnl.exe", XMAGetOutputBufferWriteOffset, state);
  //SHIM_SET_MAPPING("xboxkrnl.exe", XMAGetPacketMetadata, state);
  //SHIM_SET_MAPPING("xboxkrnl.exe", XMAEnableContext, state);
  //SHIM_SET_MAPPING("xboxkrnl.exe", XMADisableContext, state);
  //SHIM_SET_MAPPING("xboxkrnl.exe", XMABlockWhileInUse, state);
}
