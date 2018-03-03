/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APU_XMA_CONTEXT_H_
#define XENIA_APU_XMA_CONTEXT_H_

#include <atomic>
#include <mutex>
#include <queue>
#include <vector>

#include "xenia/memory.h"
#include "xenia/xbox.h"

// XMA audio format:
// From research, XMA appears to be based on WMA Pro with
// a few (very slight) modifications.
// XMA2 is fully backwards-compatible with XMA1.

// Helpful resources:
// https://github.com/koolkdev/libertyv/blob/master/libav_wrapper/xma2dec.c
// http://hcs64.com/mboard/forum.php?showthread=14818
// https://github.com/hrydgard/minidx9/blob/master/Include/xma2defs.h

// Forward declarations
struct AVCodec;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;

namespace xe {
namespace apu {

// This is stored in guest space in big-endian order.
// We load and swap the whole thing to splat here so that we can
// use bitfields.
// This could be important:
// http://www.fmod.org/questions/question/forum-15859
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
  uint32_t unk_dword_2 : 6;                // ErrorStatus/ErrorSet (?)

  // DWORD 3
  uint32_t loop_start : 26;  // XMASetLoopData LoopStartOffset
                             // frame offset in bits
  uint32_t unk_dword_3 : 6;  // ? ParserErrorStatus/ParserErrorSet(?)

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
};
static_assert_size(XMA_CONTEXT_DATA, 64);

#pragma pack(push, 1)
struct WmaProExtraData {
  uint16_t bits_per_sample;
  uint32_t channel_mask;
  uint8_t unk06[8];
  uint16_t decode_flags;
  uint8_t unk10[2];
};
static_assert_size(WmaProExtraData, 18);
#pragma pack(pop)

class XmaContext {
 public:
  static const uint32_t kBytesPerPacket = 2048;

  static const uint32_t kBytesPerSample = 2;
  static const uint32_t kSamplesPerFrame = 512;
  static const uint32_t kSamplesPerSubframe = 128;
  static const uint32_t kBytesPerFrame = kSamplesPerFrame * kBytesPerSample;
  static const uint32_t kBytesPerSubframe =
      kSamplesPerSubframe * kBytesPerSample;

  static const uint32_t kOutputBytesPerBlock = 256;
  static const uint32_t kOutputMaxSizeBytes = 31 * kOutputBytesPerBlock;

  explicit XmaContext();
  ~XmaContext();

  int Setup(uint32_t id, Memory* memory, uint32_t guest_ptr);
  bool Work();

  void Enable();
  bool Block(bool poll);
  void Clear();
  void Disable();
  void Release();

  Memory* memory() const { return memory_; }

  uint32_t id() { return id_; }
  uint32_t guest_ptr() { return guest_ptr_; }
  bool is_allocated() { return is_allocated_; }
  bool is_enabled() { return is_enabled_; }

  void set_is_allocated(bool is_allocated) { is_allocated_ = is_allocated; }
  void set_is_enabled(bool is_enabled) { is_enabled_ = is_enabled; }

 private:
  static int GetSampleRate(int id);

  size_t SavePartial(uint8_t* packet, uint32_t frame_offset_bits,
                     size_t frame_size_bits, bool append);
  bool ValidFrameOffset(uint8_t* block, size_t size_bytes,
                        size_t frame_offset_bits);
  void DecodePackets(XMA_CONTEXT_DATA* data);
  uint32_t GetFramePacketNumber(uint8_t* block, size_t size, size_t bit_offset);
  int PrepareDecoder(uint8_t* block, size_t size, int sample_rate,
                     int channels);

  bool ConvertFrame(const uint8_t** samples, int num_channels, int num_samples,
                    uint8_t* output_buffer);

  int StartPacket(XMA_CONTEXT_DATA* data);

  int PreparePacket(uint8_t* input, size_t seq_offset, size_t size,
                    int sample_rate, int channels);
  void DiscardPacket();

  int DecodePacket(uint8_t* output, size_t offset, size_t size,
                   size_t* read_bytes);

  Memory* memory_ = nullptr;

  uint32_t id_ = 0;
  uint32_t guest_ptr_ = 0;
  std::mutex lock_;
  bool is_allocated_ = false;
  bool is_enabled_ = false;

  // libav structures
  AVCodec* codec_ = nullptr;
  AVCodecContext* context_ = nullptr;
  AVFrame* decoded_frame_ = nullptr;
  AVPacket* packet_ = nullptr;
  WmaProExtraData extra_data_;

  bool partial_frame_saved_ = false;
  bool partial_frame_size_known_ = false;
  size_t partial_frame_total_size_bits_ = 0;
  size_t partial_frame_start_offset_bits_ = 0;
  size_t partial_frame_offset_bits_ = 0;  // blah internal don't use this
  std::vector<uint8_t> partial_frame_buffer_;

  uint8_t* current_frame_ = nullptr;
};

}  // namespace apu
}  // namespace xe

#endif  // XENIA_APU_XMA_CONTEXT_H_
