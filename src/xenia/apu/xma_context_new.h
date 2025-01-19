/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APU_XMA_CONTEXT_NEW_H_
#define XENIA_APU_XMA_CONTEXT_NEW_H_

#include <array>
#include <atomic>
#include <mutex>
#include <queue>

#include "xenia/apu/xma_context.h"
#include "xenia/base/bit_stream.h"
#include "xenia/base/ring_buffer.h"
#include "xenia/memory.h"
#include "xenia/xbox.h"

// Forward declarations
struct AVCodec;
struct AVCodecParserContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;

namespace xe {
namespace apu {

struct kPacketInfo {
  uint8_t frame_count_;
  uint8_t current_frame_;
  uint32_t current_frame_size_;

  const bool isLastFrameInPacket() const {
    return current_frame_ == frame_count_ - 1;
  }
};

static constexpr int kIdToSampleRate[4] = {24000, 32000, 44100, 48000};

class XmaContextNew : public XmaContext {
 public:
  static constexpr uint32_t kBytesPerPacket = 2048;
  static constexpr uint32_t kBytesPerPacketHeader = 4;
  static constexpr uint32_t kBytesPerPacketData =
      kBytesPerPacket - kBytesPerPacketHeader;

  static constexpr uint32_t kBitsPerPacket = kBytesPerPacket * 8;
  static constexpr uint32_t kBitsPerPacketHeader = 32;
  static constexpr uint32_t kBitsPerFrameHeader = 15;

  static constexpr uint32_t kBytesPerSample = 2;
  static constexpr uint32_t kSamplesPerFrame = 512;
  static constexpr uint32_t kSamplesPerSubframe = 128;
  static constexpr uint32_t kBytesPerFrameChannel =
      kSamplesPerFrame * kBytesPerSample;
  static constexpr uint32_t kBytesPerSubframeChannel =
      kSamplesPerSubframe * kBytesPerSample;

  static constexpr uint32_t kOutputBytesPerBlock = 256;
  static constexpr uint32_t kOutputMaxSizeBytes = 31 * kOutputBytesPerBlock;

  static constexpr uint32_t kLastFrameMarker = 0x7FFF;
  static constexpr uint32_t kMaxFrameSizeinBits = 0x4000 - kBitsPerPacketHeader;

  explicit XmaContextNew();
  ~XmaContextNew();

  int Setup(uint32_t id, Memory* memory, uint32_t guest_ptr);
  bool Work();

  void Enable();
  bool Block(bool poll);
  void Clear();
  void Disable();
  void Release();

 private:
  static void SwapInputBuffer(XMA_CONTEXT_DATA* data);
  // Convert sampling rate from ID to frequency.
  static int GetSampleRate(int id);
  // Get the containing packet number of the frame pointed to by the offset.
  static int16_t GetPacketNumber(size_t size, size_t bit_offset);

  const kPacketInfo GetPacketInfo(uint8_t* packet, uint32_t frame_offset);

  const uint32_t GetAmountOfBitsToRead(const uint32_t remaining_stream_bits,
                                       const uint32_t frame_size);

  const uint8_t* GetNextPacket(XMA_CONTEXT_DATA* data,
                               uint32_t next_packet_index,
                               uint32_t current_input_packet_count);

  const uint32_t GetNextPacketReadOffset(uint8_t* buffer,
                                         uint32_t next_packet_index,
                                         uint32_t current_input_packet_count);

  // Returns currently used buffer
  uint8_t* GetCurrentInputBuffer(XMA_CONTEXT_DATA* data);

  static uint32_t GetCurrentInputBufferSize(XMA_CONTEXT_DATA* data);

  void Decode(XMA_CONTEXT_DATA* data);
  void Consume(RingBuffer* output_rb, XMA_CONTEXT_DATA* data);

  void UpdateLoopStatus(XMA_CONTEXT_DATA* data);
  int PrepareDecoder(int sample_rate, bool is_two_channel);
  void PreparePacket(const uint32_t frame_size, const uint32_t frame_padding);

  RingBuffer PrepareOutputRingBuffer(XMA_CONTEXT_DATA* data);

  bool DecodePacket(AVCodecContext* av_context, const AVPacket* av_packet,
                    AVFrame* av_frame);

  // This method should be used ONLY when we're at the last packet of the stream
  // and we want to find offset in next buffer
  uint32_t GetPacketFirstFrameOffset(const XMA_CONTEXT_DATA* data);

  std::array<uint8_t, kBytesPerPacketData * 2> input_buffer_;
  // first byte contains bit offset information
  std::array<uint8_t, 1 + 4096> xma_frame_;
  std::array<uint8_t, kBytesPerFrameChannel * 2> raw_frame_;

  int32_t remaining_subframe_blocks_in_output_buffer_ = 0;
  uint8_t current_frame_remaining_subframes_ = 0;
};

}  // namespace apu
}  // namespace xe

#endif  // XENIA_APU_XMA_CONTEXT_H_