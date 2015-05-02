/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/apu/apu.h"
#include "xenia/base/logging.h"
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

// This is stored in guest space in big-endian order.
// We load and swap the whole thing to splat here so that we can
// use bitfields.
struct XMAContextData {
  static const uint32_t kSize = 64;
  static const uint32_t kBytesPerBlock = 2048;
  static const uint32_t kSamplesPerFrame = 512;
  static const uint32_t kSamplesPerSubframe = 128;

  // DWORD 0
  uint32_t input_buffer_0_block_count : 12;  // XMASetInputBuffer0, number of
                                             // 2KB blocks.
  uint32_t loop_count : 8;                   // +12bit, XMASetLoopData
  uint32_t input_buffer_0_valid : 1;         // +20bit, XMAIsInputBuffer0Valid
  uint32_t input_buffer_1_valid : 1;         // +21bit, XMAIsInputBuffer1Valid
  uint32_t output_buffer_block_count : 5;    // +22bit
  uint32_t
      output_buffer_write_offset : 5;  // +27bit, XMAGetOutputBufferWriteOffset

  // DWORD 1
  uint32_t input_buffer_1_block_count : 12;  // XMASetInputBuffer1, number of
                                             // 2KB blocks.
  uint32_t loop_subframe_end : 2;            // +12bit, XMASetLoopData
  uint32_t unk_dword_1_a : 3;                // ?
  uint32_t loop_subframe_skip : 3;           // +17bit, XMASetLoopData
  uint32_t subframe_decode_count : 4;        // +20bit
  uint32_t unk_dword_1_b : 3;                // ?
  uint32_t sample_rate : 2;                  // +27bit
  uint32_t is_stereo : 1;                    // +29bit
  uint32_t unk_dword_1_c : 1;                // ?
  uint32_t output_buffer_valid : 1;          // +31bit, XMAIsOutputBufferValid

  // DWORD 2
  uint32_t input_buffer_read_offset : 30;  // XMAGetInputBufferReadOffset
  uint32_t unk_dword_2 : 2;                // ?

  // DWORD 3
  uint32_t loop_start : 26;  // XMASetLoopData
  uint32_t unk_dword_3 : 6;  // ?

  // DWORD 4
  uint32_t loop_end : 26;        // XMASetLoopData
  uint32_t packet_metadata : 5;  // XMAGetPacketMetadata
  uint32_t current_buffer : 1;   // ?

  // DWORD 5
  uint32_t input_buffer_0_ptr;  // top bits lopped off?
                                // DWORD 6
  uint32_t input_buffer_1_ptr;  // top bits lopped off?
                                // DWORD 7
  uint32_t output_buffer_ptr;   // top bits lopped off?
                                // DWORD 8
  uint32_t unk_dword_8;         // Some kind of pointer like output_buffer_ptr

  // DWORD 9
  uint32_t
      output_buffer_read_offset : 5;  // +0bit, XMAGetOutputBufferReadOffset
  uint32_t unk_dword_9 : 27;

  XMAContextData(const void* ptr) {
    xe::copy_and_swap_32_aligned(reinterpret_cast<uint32_t*>(this),
                                 reinterpret_cast<const uint32_t*>(ptr),
                                 sizeof(XMAContextData) / 4);
  }

  void Store(void* ptr) {
    xe::copy_and_swap_32_aligned(reinterpret_cast<uint32_t*>(ptr),
                                 reinterpret_cast<const uint32_t*>(this),
                                 sizeof(XMAContextData) / 4);
  }
};
static_assert(sizeof(XMAContextData) == 4 * 10, "Must be packed");

void StoreXmaContextIndexedRegister(KernelState* state, uint32_t base_reg,
                                    uint32_t context_ptr) {
  auto audio_system = state->emulator()->audio_system();
  uint32_t hw_index = (context_ptr - audio_system->xma_context_array_ptr()) /
                      XMAContextData::kSize;
  uint32_t reg_num = base_reg + (hw_index >> 5) * 4;
  uint32_t reg_value = 1 << (hw_index & 0x1F);
  xe::store<uint32_t>(state->memory()->TranslateVirtual(0x7FEA0000 + reg_num),
                      reg_value);
}

SHIM_CALL XMAInitializeContext_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t context_ptr = SHIM_GET_ARG_32(0);
  uint32_t context_init_ptr = SHIM_GET_ARG_32(1);

  XELOGD("XMAInitializeContext(%.8X, %.8X)", context_ptr, context_init_ptr);

  std::memset(SHIM_MEM_ADDR(context_ptr), 0, XMAContextData::kSize);

  XMAContextData context(SHIM_MEM_ADDR(context_ptr));

  context.input_buffer_0_ptr = SHIM_MEM_32(context_init_ptr + 0 * 4);
  context.input_buffer_0_block_count = SHIM_MEM_32(context_init_ptr + 1 * 4);
  context.input_buffer_1_ptr = SHIM_MEM_32(context_init_ptr + 2 * 4);
  context.input_buffer_1_block_count = SHIM_MEM_32(context_init_ptr + 3 * 4);
  context.input_buffer_read_offset = SHIM_MEM_32(context_init_ptr + 4 * 4);
  context.output_buffer_ptr = SHIM_MEM_32(context_init_ptr + 5 * 4);
  context.output_buffer_block_count = SHIM_MEM_32(context_init_ptr + 6 * 4);

  // context.work_buffer = SHIM_MEM_32(context_init_ptr + 7 * 4);  // ?
  context.subframe_decode_count = SHIM_MEM_32(context_init_ptr + 8 * 4);
  context.is_stereo = SHIM_MEM_32(context_init_ptr + 9 * 4) == 2;
  context.sample_rate = SHIM_MEM_32(context_init_ptr + 10 * 4);

  uint32_t loop_data_ptr = context_init_ptr + 11 * 4;
  context.loop_start = SHIM_MEM_32(loop_data_ptr + 0);
  context.loop_end = SHIM_MEM_32(loop_data_ptr + 4);
  context.loop_count = SHIM_MEM_8(loop_data_ptr + 6);
  context.loop_subframe_end = SHIM_MEM_8(loop_data_ptr + 6);
  context.loop_subframe_skip = SHIM_MEM_8(loop_data_ptr + 7);

  context.Store(SHIM_MEM_ADDR(context_ptr));

  StoreXmaContextIndexedRegister(state, 0x1A80, context_ptr);

  SHIM_SET_RETURN_32(0);
}

SHIM_CALL XMASetLoopData_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t context_ptr = SHIM_GET_ARG_32(0);
  uint32_t loop_data_ptr = SHIM_GET_ARG_32(1);

  XELOGD("XMASetLoopData(%.8X, %.8X)", context_ptr, loop_data_ptr);

  XMAContextData context(SHIM_MEM_ADDR(context_ptr));

  context.loop_start = SHIM_MEM_32(loop_data_ptr + 0);
  context.loop_end = SHIM_MEM_32(loop_data_ptr + 4);
  context.loop_count = SHIM_MEM_8(loop_data_ptr + 6);
  context.loop_subframe_end = SHIM_MEM_8(loop_data_ptr + 6);
  context.loop_subframe_skip = SHIM_MEM_8(loop_data_ptr + 7);

  context.Store(SHIM_MEM_ADDR(context_ptr));

  SHIM_SET_RETURN_32(0);
}

SHIM_CALL XMAGetInputBufferReadOffset_shim(PPCContext* ppc_state,
                                           KernelState* state) {
  uint32_t context_ptr = SHIM_GET_ARG_32(0);

  XELOGD("XMAGetInputBufferReadOffset(%.8X)", context_ptr);

  XMAContextData context(SHIM_MEM_ADDR(context_ptr));

  uint32_t result = context.input_buffer_read_offset;

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL XMASetInputBufferReadOffset_shim(PPCContext* ppc_state,
                                           KernelState* state) {
  uint32_t context_ptr = SHIM_GET_ARG_32(0);
  uint32_t value = SHIM_GET_ARG_32(1);

  XELOGD("XMASetInputBufferReadOffset(%.8X, %.8X)", context_ptr, value);

  XMAContextData context(SHIM_MEM_ADDR(context_ptr));

  context.input_buffer_read_offset = value;

  context.Store(SHIM_MEM_ADDR(context_ptr));

  SHIM_SET_RETURN_32(0);
}

SHIM_CALL XMASetInputBuffer0_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t context_ptr = SHIM_GET_ARG_32(0);
  uint32_t buffer_ptr = SHIM_GET_ARG_32(1);
  uint32_t block_count = SHIM_GET_ARG_32(2);

  XELOGD("XMASetInputBuffer0(%.8X, %.8X, %d)", context_ptr, buffer_ptr,
         block_count);

  XMAContextData context(SHIM_MEM_ADDR(context_ptr));

  context.input_buffer_0_ptr = buffer_ptr;
  context.input_buffer_0_block_count = block_count;
  context.input_buffer_read_offset = 32;  // in bits
  context.input_buffer_0_valid = buffer_ptr ? 1 : 0;

  context.Store(SHIM_MEM_ADDR(context_ptr));

  SHIM_SET_RETURN_32(0);
}

SHIM_CALL XMAIsInputBuffer0Valid_shim(PPCContext* ppc_state,
                                      KernelState* state) {
  uint32_t context_ptr = SHIM_GET_ARG_32(0);

  XELOGD("XMAIsInputBuffer0Valid(%.8X)", context_ptr);

  XMAContextData context(SHIM_MEM_ADDR(context_ptr));

  uint32_t result = context.input_buffer_0_valid;

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL XMASetInputBuffer0Valid_shim(PPCContext* ppc_state,
                                       KernelState* state) {
  uint32_t context_ptr = SHIM_GET_ARG_32(0);

  XELOGD("XMASetInputBuffer0Valid(%.8X)", context_ptr);

  XMAContextData context(SHIM_MEM_ADDR(context_ptr));

  context.input_buffer_0_valid = 1;

  context.Store(SHIM_MEM_ADDR(context_ptr));

  SHIM_SET_RETURN_32(0);
}

SHIM_CALL XMASetInputBuffer1_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t context_ptr = SHIM_GET_ARG_32(0);
  uint32_t buffer_ptr = SHIM_GET_ARG_32(1);
  uint32_t block_count = SHIM_GET_ARG_32(2);

  XELOGD("XMASetInputBuffer1(%.8X, %.8X, %d)", context_ptr, buffer_ptr,
         block_count);

  XMAContextData context(SHIM_MEM_ADDR(context_ptr));

  context.input_buffer_1_ptr = buffer_ptr;
  context.input_buffer_1_block_count = block_count;
  context.input_buffer_read_offset = 32;  // in bits
  context.input_buffer_1_valid = buffer_ptr ? 1 : 0;

  context.Store(SHIM_MEM_ADDR(context_ptr));

  SHIM_SET_RETURN_32(0);
}

SHIM_CALL XMAIsInputBuffer1Valid_shim(PPCContext* ppc_state,
                                      KernelState* state) {
  uint32_t context_ptr = SHIM_GET_ARG_32(0);

  XELOGD("XMAIsInputBuffer1Valid(%.8X)", context_ptr);

  XMAContextData context(SHIM_MEM_ADDR(context_ptr));

  uint32_t result = context.input_buffer_1_valid;

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL XMASetInputBuffer1Valid_shim(PPCContext* ppc_state,
                                       KernelState* state) {
  uint32_t context_ptr = SHIM_GET_ARG_32(0);

  XELOGD("XMASetInputBuffer1Valid(%.8X)", context_ptr);

  XMAContextData context(SHIM_MEM_ADDR(context_ptr));

  context.input_buffer_1_valid = 1;

  context.Store(SHIM_MEM_ADDR(context_ptr));

  SHIM_SET_RETURN_32(0);
}

SHIM_CALL XMAIsOutputBufferValid_shim(PPCContext* ppc_state,
                                      KernelState* state) {
  uint32_t context_ptr = SHIM_GET_ARG_32(0);

  XELOGD("XMAIsOutputBufferValid(%.8X)", context_ptr);

  XMAContextData context(SHIM_MEM_ADDR(context_ptr));

  uint32_t result = context.output_buffer_valid;

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL XMASetOutputBufferValid_shim(PPCContext* ppc_state,
                                       KernelState* state) {
  uint32_t context_ptr = SHIM_GET_ARG_32(0);

  XELOGD("XMASetOutputBufferValid(%.8X)", context_ptr);

  XMAContextData context(SHIM_MEM_ADDR(context_ptr));

  context.output_buffer_valid = 1;

  context.Store(SHIM_MEM_ADDR(context_ptr));

  SHIM_SET_RETURN_32(0);
}

SHIM_CALL XMAGetOutputBufferReadOffset_shim(PPCContext* ppc_state,
                                            KernelState* state) {
  uint32_t context_ptr = SHIM_GET_ARG_32(0);

  XELOGD("XMAGetOutputBufferReadOffset(%.8X)", context_ptr);

  XMAContextData context(SHIM_MEM_ADDR(context_ptr));

  uint32_t result = context.output_buffer_read_offset;

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL XMASetOutputBufferReadOffset_shim(PPCContext* ppc_state,
                                            KernelState* state) {
  uint32_t context_ptr = SHIM_GET_ARG_32(0);
  uint32_t value = SHIM_GET_ARG_32(1);

  XELOGD("XMASetOutputBufferReadOffset(%.8X, %.8X)", context_ptr, value);

  XMAContextData context(SHIM_MEM_ADDR(context_ptr));

  context.output_buffer_read_offset = value;

  context.Store(SHIM_MEM_ADDR(context_ptr));

  SHIM_SET_RETURN_32(0);
}

SHIM_CALL XMAGetOutputBufferWriteOffset_shim(PPCContext* ppc_state,
                                             KernelState* state) {
  uint32_t context_ptr = SHIM_GET_ARG_32(0);

  XELOGD("XMAGetOutputBufferWriteOffset(%.8X)", context_ptr);

  XMAContextData context(SHIM_MEM_ADDR(context_ptr));

  uint32_t result = context.output_buffer_write_offset;

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL XMAGetPacketMetadata_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t context_ptr = SHIM_GET_ARG_32(0);

  XELOGD("XMAGetPacketMetadata(%.8X)", context_ptr);

  XMAContextData context(SHIM_MEM_ADDR(context_ptr));

  uint32_t result = context.packet_metadata;

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL XMAEnableContext_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t context_ptr = SHIM_GET_ARG_32(0);

  XELOGD("XMAEnableContext(%.8X)", context_ptr);

  StoreXmaContextIndexedRegister(state, 0x1940, context_ptr);

  SHIM_SET_RETURN_32(0);
}

SHIM_CALL XMADisableContext_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t context_ptr = SHIM_GET_ARG_32(0);
  uint32_t wait = SHIM_GET_ARG_32(1);

  XELOGD("XMADisableContext(%.8X, %d)", context_ptr, wait);

  X_HRESULT result = X_E_SUCCESS;
  StoreXmaContextIndexedRegister(state, 0x1A40, context_ptr);
  XMAContextData context(SHIM_MEM_ADDR(context_ptr));

  SHIM_SET_RETURN_32(result);
}

SHIM_CALL XMABlockWhileInUse_shim(PPCContext* ppc_state, KernelState* state) {
  uint32_t context_ptr = SHIM_GET_ARG_32(0);

  XELOGD("XMABlockWhileInUse(%.8X)", context_ptr);

  do {
    XMAContextData context(SHIM_MEM_ADDR(context_ptr));
    if (!context.input_buffer_0_valid && !context.input_buffer_1_valid) {
      break;
    }
    Sleep(1);
  } while (true);

  SHIM_SET_RETURN_32(0);
}

}  // namespace kernel
}  // namespace xe

void xe::kernel::xboxkrnl::RegisterAudioXmaExports(
    xe::cpu::ExportResolver* export_resolver, KernelState* state) {
  // Used for both XMA* methods and direct register access.
  SHIM_SET_MAPPING("xboxkrnl.exe", XMACreateContext, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", XMAReleaseContext, state);

  // Only used in older games.
  SHIM_SET_MAPPING("xboxkrnl.exe", XMAInitializeContext, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", XMASetLoopData, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", XMAGetInputBufferReadOffset, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", XMASetInputBufferReadOffset, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", XMASetInputBuffer0, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", XMAIsInputBuffer0Valid, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", XMASetInputBuffer0Valid, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", XMASetInputBuffer1, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", XMAIsInputBuffer1Valid, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", XMASetInputBuffer1Valid, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", XMAIsOutputBufferValid, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", XMASetOutputBufferValid, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", XMASetOutputBufferReadOffset, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", XMAGetOutputBufferReadOffset, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", XMAGetOutputBufferWriteOffset, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", XMAGetPacketMetadata, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", XMAEnableContext, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", XMADisableContext, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", XMABlockWhileInUse, state);
}
