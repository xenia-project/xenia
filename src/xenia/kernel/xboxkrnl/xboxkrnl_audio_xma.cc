/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <cstring>

#include "xenia/apu/audio_system.h"
#include "xenia/apu/xma_decoder.h"
#include "xenia/base/logging.h"
#include "xenia/emulator.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_private.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xboxkrnl {

using xe::apu::XMA_CONTEXT_DATA;

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

dword_result_t XMACreateContext(lpdword_t context_out_ptr) {
  auto xma_decoder = kernel_state()->emulator()->audio_system()->xma_decoder();
  uint32_t context_ptr = xma_decoder->AllocateContext();
  *context_out_ptr = context_ptr;
  if (!context_ptr) {
    return X_STATUS_NO_MEMORY;
  }
  return X_STATUS_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT(XMACreateContext, ExportTag::kImplemented |
                                              ExportTag::kAudio |
                                              ExportTag::kHighFrequency);

dword_result_t XMAReleaseContext(lpvoid_t context_ptr) {
  auto xma_decoder = kernel_state()->emulator()->audio_system()->xma_decoder();
  xma_decoder->ReleaseContext(context_ptr);
  return 0;
}
DECLARE_XBOXKRNL_EXPORT(XMAReleaseContext, ExportTag::kImplemented |
                                               ExportTag::kAudio |
                                               ExportTag::kHighFrequency);

void StoreXmaContextIndexedRegister(KernelState* kernel_state,
                                    uint32_t base_reg, uint32_t context_ptr) {
  auto xma_decoder = kernel_state->emulator()->audio_system()->xma_decoder();
  uint32_t hw_index = (context_ptr - xma_decoder->context_array_ptr()) /
                      sizeof(XMA_CONTEXT_DATA);
  uint32_t reg_num = base_reg + (hw_index >> 5) * 4;
  uint32_t reg_value = 1 << (hw_index & 0x1F);
  xma_decoder->WriteRegister(reg_num, xe::byte_swap(reg_value));
}

struct XMA_LOOP_DATA {
  xe::be<uint32_t> loop_start;
  xe::be<uint32_t> loop_end;
  uint8_t loop_count;
  uint8_t loop_subframe_end;
  uint8_t loop_subframe_skip;
};
static_assert_size(XMA_LOOP_DATA, 12);

struct XMA_CONTEXT_INIT {
  xe::be<uint32_t> input_buffer_0_ptr;
  xe::be<uint32_t> input_buffer_0_packet_count;
  xe::be<uint32_t> input_buffer_1_ptr;
  xe::be<uint32_t> input_buffer_1_packet_count;
  xe::be<uint32_t> input_buffer_read_offset;
  xe::be<uint32_t> output_buffer_ptr;
  xe::be<uint32_t> output_buffer_block_count;
  xe::be<uint32_t> work_buffer;
  xe::be<uint32_t> subframe_decode_count;
  xe::be<uint32_t> channel_count;
  xe::be<uint32_t> sample_rate;
  XMA_LOOP_DATA loop_data;
};
static_assert_size(XMA_CONTEXT_INIT, 56);

dword_result_t XMAInitializeContext(lpvoid_t context_ptr,
                                    pointer_t<XMA_CONTEXT_INIT> context_init) {
  std::memset(context_ptr, 0, sizeof(XMA_CONTEXT_DATA));

  XMA_CONTEXT_DATA context(context_ptr);

  context.input_buffer_0_ptr = context_init->input_buffer_0_ptr;
  context.input_buffer_0_packet_count =
      context_init->input_buffer_0_packet_count;
  context.input_buffer_1_ptr = context_init->input_buffer_1_ptr;
  context.input_buffer_1_packet_count =
      context_init->input_buffer_1_packet_count;
  context.input_buffer_read_offset = context_init->input_buffer_read_offset;
  context.output_buffer_ptr = context_init->output_buffer_ptr;
  context.output_buffer_block_count = context_init->output_buffer_block_count;

  // context.work_buffer = context_init->work_buffer;  // ?
  context.subframe_decode_count = context_init->subframe_decode_count;
  context.is_stereo = context_init->channel_count >= 1;
  context.sample_rate = context_init->sample_rate;

  context.loop_start = context_init->loop_data.loop_start;
  context.loop_end = context_init->loop_data.loop_end;
  context.loop_count = context_init->loop_data.loop_count;
  context.loop_subframe_end = context_init->loop_data.loop_subframe_end;
  context.loop_subframe_skip = context_init->loop_data.loop_subframe_skip;

  context.Store(context_ptr);

  StoreXmaContextIndexedRegister(kernel_state(), 0x1A80, context_ptr);

  return 0;
}
DECLARE_XBOXKRNL_EXPORT(XMAInitializeContext, ExportTag::kImplemented |
                                                  ExportTag::kAudio |
                                                  ExportTag::kHighFrequency);

dword_result_t XMASetLoopData(lpvoid_t context_ptr,
                              pointer_t<XMA_CONTEXT_DATA> loop_data) {
  XMA_CONTEXT_DATA context(context_ptr);

  context.loop_start = loop_data->loop_start;
  context.loop_end = loop_data->loop_end;
  context.loop_count = loop_data->loop_count;
  context.loop_subframe_end = loop_data->loop_subframe_end;
  context.loop_subframe_skip = loop_data->loop_subframe_skip;

  context.Store(context_ptr);

  return 0;
}
DECLARE_XBOXKRNL_EXPORT(XMASetLoopData, ExportTag::kImplemented |
                                            ExportTag::kAudio |
                                            ExportTag::kHighFrequency);

dword_result_t XMAGetInputBufferReadOffset(lpvoid_t context_ptr) {
  XMA_CONTEXT_DATA context(context_ptr);
  return context.input_buffer_read_offset;
}
DECLARE_XBOXKRNL_EXPORT(XMAGetInputBufferReadOffset,
                        ExportTag::kImplemented | ExportTag::kAudio |
                            ExportTag::kHighFrequency);

dword_result_t XMASetInputBufferReadOffset(lpvoid_t context_ptr,
                                           dword_t value) {
  XMA_CONTEXT_DATA context(context_ptr);
  context.input_buffer_read_offset = value;
  context.Store(context_ptr);

  return 0;
}
DECLARE_XBOXKRNL_EXPORT(XMASetInputBufferReadOffset,
                        ExportTag::kImplemented | ExportTag::kAudio |
                            ExportTag::kHighFrequency);

dword_result_t XMASetInputBuffer0(lpvoid_t context_ptr, lpvoid_t buffer,
                                  dword_t packet_count) {
  XMA_CONTEXT_DATA context(context_ptr);

  context.input_buffer_0_ptr = buffer.guest_address();
  context.input_buffer_0_packet_count = packet_count;

  context.Store(context_ptr);

  return 0;
}
DECLARE_XBOXKRNL_EXPORT(XMASetInputBuffer0, ExportTag::kImplemented |
                                                ExportTag::kAudio |
                                                ExportTag::kHighFrequency);

dword_result_t XMAIsInputBuffer0Valid(lpvoid_t context_ptr) {
  XMA_CONTEXT_DATA context(context_ptr);
  return context.input_buffer_0_valid;
}
DECLARE_XBOXKRNL_EXPORT(XMAIsInputBuffer0Valid, ExportTag::kImplemented |
                                                    ExportTag::kAudio |
                                                    ExportTag::kHighFrequency);

dword_result_t XMASetInputBuffer0Valid(lpvoid_t context_ptr) {
  XMA_CONTEXT_DATA context(context_ptr);
  context.input_buffer_0_valid = 1;
  context.Store(context_ptr);

  return 0;
}
DECLARE_XBOXKRNL_EXPORT(XMASetInputBuffer0Valid, ExportTag::kImplemented |
                                                     ExportTag::kAudio |
                                                     ExportTag::kHighFrequency);

dword_result_t XMASetInputBuffer1(lpvoid_t context_ptr, lpvoid_t buffer,
                                  dword_t packet_count) {
  XMA_CONTEXT_DATA context(context_ptr);

  context.input_buffer_1_ptr = buffer.guest_address();
  context.input_buffer_1_packet_count = packet_count;

  context.Store(context_ptr);

  return 0;
}
DECLARE_XBOXKRNL_EXPORT(XMASetInputBuffer1, ExportTag::kImplemented |
                                                ExportTag::kAudio |
                                                ExportTag::kHighFrequency);

dword_result_t XMAIsInputBuffer1Valid(lpvoid_t context_ptr) {
  XMA_CONTEXT_DATA context(context_ptr);
  return context.input_buffer_1_valid;
}
DECLARE_XBOXKRNL_EXPORT(XMAIsInputBuffer1Valid, ExportTag::kImplemented |
                                                    ExportTag::kAudio |
                                                    ExportTag::kHighFrequency);

dword_result_t XMASetInputBuffer1Valid(lpvoid_t context_ptr) {
  XMA_CONTEXT_DATA context(context_ptr);
  context.input_buffer_1_valid = 1;
  context.Store(context_ptr);

  return 0;
}
DECLARE_XBOXKRNL_EXPORT(XMASetInputBuffer1Valid, ExportTag::kImplemented |
                                                     ExportTag::kAudio |
                                                     ExportTag::kHighFrequency);

dword_result_t XMAIsOutputBufferValid(lpvoid_t context_ptr) {
  XMA_CONTEXT_DATA context(context_ptr);
  return context.output_buffer_valid;
}
DECLARE_XBOXKRNL_EXPORT(XMAIsOutputBufferValid, ExportTag::kImplemented |
                                                    ExportTag::kAudio |
                                                    ExportTag::kHighFrequency);

dword_result_t XMASetOutputBufferValid(lpvoid_t context_ptr) {
  XMA_CONTEXT_DATA context(context_ptr);
  context.output_buffer_valid = 1;
  context.Store(context_ptr);

  return 0;
}
DECLARE_XBOXKRNL_EXPORT(XMASetOutputBufferValid, ExportTag::kImplemented |
                                                     ExportTag::kAudio |
                                                     ExportTag::kHighFrequency);

dword_result_t XMAGetOutputBufferReadOffset(lpvoid_t context_ptr) {
  XMA_CONTEXT_DATA context(context_ptr);
  return context.output_buffer_read_offset;
}
DECLARE_XBOXKRNL_EXPORT(XMAGetOutputBufferReadOffset,
                        ExportTag::kImplemented | ExportTag::kAudio |
                            ExportTag::kHighFrequency);

dword_result_t XMASetOutputBufferReadOffset(lpvoid_t context_ptr,
                                            dword_t value) {
  XMA_CONTEXT_DATA context(context_ptr);
  context.output_buffer_read_offset = value;
  context.Store(context_ptr);

  return 0;
}
DECLARE_XBOXKRNL_EXPORT(XMASetOutputBufferReadOffset,
                        ExportTag::kImplemented | ExportTag::kAudio |
                            ExportTag::kHighFrequency);

dword_result_t XMAGetOutputBufferWriteOffset(lpvoid_t context_ptr) {
  XMA_CONTEXT_DATA context(context_ptr);
  return context.output_buffer_write_offset;
}
DECLARE_XBOXKRNL_EXPORT(XMAGetOutputBufferWriteOffset,
                        ExportTag::kImplemented | ExportTag::kAudio |
                            ExportTag::kHighFrequency);

dword_result_t XMAGetPacketMetadata(lpvoid_t context_ptr) {
  XMA_CONTEXT_DATA context(context_ptr);
  return context.packet_metadata;
}
DECLARE_XBOXKRNL_EXPORT(XMAGetPacketMetadata,
                        ExportTag::kImplemented | ExportTag::kAudio);

dword_result_t XMAEnableContext(lpvoid_t context_ptr) {
  StoreXmaContextIndexedRegister(kernel_state(), 0x1940, context_ptr);
  return 0;
}
DECLARE_XBOXKRNL_EXPORT(XMAEnableContext, ExportTag::kImplemented |
                                              ExportTag::kAudio |
                                              ExportTag::kHighFrequency);

dword_result_t XMADisableContext(lpvoid_t context_ptr, dword_t wait) {
  X_HRESULT result = X_E_SUCCESS;
  StoreXmaContextIndexedRegister(kernel_state(), 0x1A40, context_ptr);
  if (!kernel_state()
           ->emulator()
           ->audio_system()
           ->xma_decoder()
           ->BlockOnContext(context_ptr, !wait)) {
    result = X_E_FALSE;
  }
  return result;
}
DECLARE_XBOXKRNL_EXPORT(XMADisableContext, ExportTag::kImplemented |
                                               ExportTag::kAudio |
                                               ExportTag::kHighFrequency);

dword_result_t XMABlockWhileInUse(lpvoid_t context_ptr) {
  do {
    XMA_CONTEXT_DATA context(context_ptr);
    if (!context.input_buffer_0_valid && !context.input_buffer_1_valid) {
      break;
    }
    xe::threading::Sleep(std::chrono::milliseconds(1));
  } while (true);
  return 0;
}
DECLARE_XBOXKRNL_EXPORT(XMABlockWhileInUse, ExportTag::kImplemented |
                                                ExportTag::kAudio |
                                                ExportTag::kHighFrequency);

void RegisterAudioXmaExports(xe::cpu::ExportResolver* export_resolver,
                             KernelState* kernel_state) {}

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe
