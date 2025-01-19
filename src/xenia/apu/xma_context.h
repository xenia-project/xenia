/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APU_XMA_CONTEXT_H_
#define XENIA_APU_XMA_CONTEXT_H_

#include <array>
#include <atomic>
#include <mutex>
#include <queue>
// #include <vector>

#include "xenia/memory.h"
#include "xenia/xbox.h"

// XMA audio format:
// From research, XMA appears to be based on WMA Pro with
// a few (very slight) modifications.
// XMA2 is fully backwards-compatible with XMA1.

// Helpful resources:
// https://github.com/koolkdev/libertyv/blob/master/libav_wrapper/xma2dec.c
// https://hcs64.com/mboard/forum.php?showthread=14818
// https://github.com/hrydgard/minidx9/blob/master/Include/xma2defs.h

// Forward declarations
struct AVCodec;
struct AVCodecParserContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;

namespace xe {
namespace apu {

// This is stored in guest space in big-endian order.
// We load and swap the whole thing to splat here so that we can
// use bitfields.
// This could be important:
// https://www.fmod.org/questions/question/forum-15859
// Appears to be dumped in order (for the most part)

struct XMA_CONTEXT_DATA {
  // DWORD 0
  uint32_t input_buffer_0_packet_count : 12;  // XMASetInputBuffer0, number of
                                              // 2KB packets. Max 4095 packets.
                                              // These packets form a block.
  uint32_t loop_count : 8;                    // +12bit, XMASetLoopData NumLoops
  uint32_t input_buffer_0_valid : 1;          // +20bit, XMAIsInputBuffer0Valid
  uint32_t input_buffer_1_valid : 1;          // +21bit, XMAIsInputBuffer1Valid
  uint32_t output_buffer_block_count : 5;     // +22bit SizeWrite 256byte blocks
  uint32_t output_buffer_write_offset : 5;    // +27bit
                                              // XMAGetOutputBufferWriteOffset
                                              // AKA OffsetWrite

  // DWORD 1
  uint32_t input_buffer_1_packet_count : 12;  // XMASetInputBuffer1, number of
                                              // 2KB packets. Max 4095 packets.
                                              // These packets form a block.
  uint32_t loop_subframe_start : 2;           // +12bit, XMASetLoopData
  uint32_t loop_subframe_end : 3;             // +14bit, XMASetLoopData
  uint32_t loop_subframe_skip : 3;            // +17bit, XMASetLoopData might be
                                              // subframe_decode_count
  uint32_t subframe_decode_count : 4;         // +20bit
  uint32_t subframe_skip_count : 3;           // +24bit
  uint32_t sample_rate : 2;                   // +27bit enum of sample rates
  uint32_t is_stereo : 1;                     // +29bit
  uint32_t unk_dword_1_c : 1;                 // +30bit
  uint32_t output_buffer_valid : 1;           // +31bit, XMAIsOutputBufferValid

  // DWORD 2
  uint32_t input_buffer_read_offset : 26;  // XMAGetInputBufferReadOffset
  uint32_t error_status : 6;               // ErrorStatus/ErrorSet (?)

  // DWORD 3
  uint32_t loop_start : 26;          // XMASetLoopData LoopStartOffset
                                     // frame offset in bits
  uint32_t parser_error_status : 6;  // ? ParserErrorStatus/ParserErrorSet(?)

  // DWORD 4
  uint32_t loop_end : 26;        // XMASetLoopData LoopEndOffset
                                 // frame offset in bits
  uint32_t packet_metadata : 5;  // XMAGetPacketMetadata
  uint32_t current_buffer : 1;   // ?

  // DWORD 5
  uint32_t input_buffer_0_ptr;  // physical address
  // DWORD 6
  uint32_t input_buffer_1_ptr;  // physical address
  // DWORD 7
  uint32_t output_buffer_ptr;  // physical address
  // DWORD 8
  uint32_t work_buffer_ptr;  // PtrOverlapAdd(?)

  // DWORD 9
  // +0bit, XMAGetOutputBufferReadOffset AKA WriteBufferOffsetRead
  uint32_t output_buffer_read_offset : 5;
  uint32_t : 25;
  uint32_t stop_when_done : 1;       // +30bit
  uint32_t interrupt_when_done : 1;  // +31bit

  // DWORD 10-15
  uint32_t unk_dwords_10_15[6];  // reserved?

  explicit XMA_CONTEXT_DATA(const void* ptr) {
    xe::copy_and_swap(reinterpret_cast<uint32_t*>(this),
                      reinterpret_cast<const uint32_t*>(ptr),
                      sizeof(XMA_CONTEXT_DATA) / 4);
  }

  void Store(void* ptr) {
    xe::copy_and_swap(reinterpret_cast<uint32_t*>(ptr),
                      reinterpret_cast<const uint32_t*>(this),
                      sizeof(XMA_CONTEXT_DATA) / 4);
  }

  bool IsInputBufferValid(uint8_t buffer_index) const {
    return buffer_index == 0 ? input_buffer_0_valid : input_buffer_1_valid;
  }

  bool IsCurrentInputBufferValid() const {
    return IsInputBufferValid(current_buffer);
  }

  bool IsAnyInputBufferValid() const {
    return input_buffer_0_valid || input_buffer_1_valid;
  }

  const uint32_t GetInputBufferAddress(uint8_t buffer_index) const {
    return buffer_index == 0 ? input_buffer_0_ptr : input_buffer_1_ptr;
  }

  const uint32_t GetCurrentInputBufferAddress() const {
    return GetInputBufferAddress(current_buffer);
  }

  const uint32_t GetInputBufferPacketCount(uint8_t buffer_index) const {
    return buffer_index == 0 ? input_buffer_0_packet_count
                             : input_buffer_1_packet_count;
  }
  const uint32_t GetCurrentInputBufferPacketCount() const {
    return GetInputBufferPacketCount(current_buffer);
  }
};
static_assert_size(XMA_CONTEXT_DATA, 64);

#pragma pack(push, 1)
// XMA2WAVEFORMATEX
struct Xma2ExtraData {
  uint8_t raw[34];
};
static_assert_size(Xma2ExtraData, 34);
#pragma pack(pop)

class XmaContext {
 public:
  static constexpr uint32_t kBytesPerPacket = 2048;
  static constexpr uint32_t kBitsPerPacket = kBytesPerPacket * 8;
  static constexpr uint32_t kBitsPerHeader = 32;

  static constexpr uint32_t kBytesPerSample = 2;
  static constexpr uint32_t kSamplesPerFrame = 512;
  static constexpr uint32_t kSamplesPerSubframe = 128;
  static constexpr uint32_t kBytesPerFrameChannel =
      kSamplesPerFrame * kBytesPerSample;
  static constexpr uint32_t kBytesPerSubframeChannel =
      kSamplesPerSubframe * kBytesPerSample;

  // static const uint32_t kOutputBytesPerBlock = 256;
  // static const uint32_t kOutputMaxSizeBytes = 31 * kOutputBytesPerBlock;

  explicit XmaContext();
  virtual ~XmaContext();

  virtual int Setup(uint32_t id, Memory* memory, uint32_t guest_ptr) {
    return 0;
  };
  virtual bool Work() { return false; };

  virtual void Enable() {};
  virtual bool Block(bool poll) { return 0; };
  virtual void Clear() {};
  virtual void Disable() {};
  virtual void Release() {};

  Memory* memory() const { return memory_; }

  uint32_t id() { return id_; }
  uint32_t guest_ptr() { return guest_ptr_; }
  bool is_allocated() { return is_allocated_; }
  bool is_enabled() { return is_enabled_; }

  void set_is_allocated(bool is_allocated) { is_allocated_ = is_allocated; }
  void set_is_enabled(bool is_enabled) { is_enabled_ = is_enabled; }

 protected:
  static void DumpRaw(AVFrame* frame, int id);
  // Convert sample format and swap bytes
  static void ConvertFrame(const uint8_t** samples, bool is_two_channel,
                           uint8_t* output_buffer);

  Memory* memory_ = nullptr;

  uint32_t id_ = 0;
  uint32_t guest_ptr_ = 0;
  xe_mutex lock_;
  volatile bool is_allocated_ = false;
  volatile bool is_enabled_ = false;

  // ffmpeg structures
  AVPacket* av_packet_ = nullptr;
  AVCodec* av_codec_ = nullptr;
  AVCodecContext* av_context_ = nullptr;
  AVFrame* av_frame_ = nullptr;
};

}  // namespace apu
}  // namespace xe

#endif  // XENIA_APU_XMA_CONTEXT_H_
