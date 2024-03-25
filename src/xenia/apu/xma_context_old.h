/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APU_XMA_CONTEXT_OLD_H_
#define XENIA_APU_XMA_CONTEXT_OLD_H_

#include <array>
#include <atomic>
#include <mutex>
#include <queue>

#include "xenia/apu/xma_context.h"
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

class XmaContextOld : public XmaContext {
 public:
  explicit XmaContextOld();
  ~XmaContextOld();

  int Setup(uint32_t id, Memory* memory, uint32_t guest_ptr);
  bool Work();

  void Enable();
  bool Block(bool poll);
  void Clear();
  void Disable();
  void Release();

 private:
  static void SwapInputBuffer(XMA_CONTEXT_DATA* data);
  static bool TrySetupNextLoop(XMA_CONTEXT_DATA* data,
                               bool ignore_input_buffer_offset);
  static void NextPacket(XMA_CONTEXT_DATA* data);
  static int GetSampleRate(int id);
  // Get the offset of the next frame. Does not traverse packets.
  static size_t GetNextFrame(uint8_t* block, size_t size, size_t bit_offset);
  // Get the containing packet number of the frame pointed to by the offset.
  static int GetFramePacketNumber(uint8_t* block, size_t size,
                                  size_t bit_offset);
  // Get the packet number and the index of the frame inside that packet
  static std::tuple<int, int> GetFrameNumber(uint8_t* block, size_t size,
                                             size_t bit_offset);
  // Get the number of frames contained in the packet (including truncated) and
  // if the last frame is split.
  static std::tuple<int, bool> GetPacketFrameCount(uint8_t* packet);

  bool ValidFrameOffset(uint8_t* block, size_t size_bytes,
                        size_t frame_offset_bits);
  void Decode(XMA_CONTEXT_DATA* data);
  int PrepareDecoder(uint8_t* packet, int sample_rate, bool is_two_channel);

  // This method should be used ONLY when we're at the last packet of the stream
  // and we want to find offset in next buffer
  uint32_t GetPacketFirstFrameOffset(const XMA_CONTEXT_DATA* data);

  // uint32_t decoded_consumed_samples_ = 0; // TODO do this dynamically
  // int decoded_idx_ = -1;

  // bool partial_frame_saved_ = false;
  // bool partial_frame_size_known_ = false;
  // size_t partial_frame_total_size_bits_ = 0;
  // size_t partial_frame_start_offset_bits_ = 0;
  // size_t partial_frame_offset_bits_ = 0;  // blah internal don't use this
  // std::vector<uint8_t> partial_frame_buffer_;
  uint32_t packets_skip_ = 0;

  bool is_stream_done_ = false;
  // bool split_frame_pending_ = false;
  uint32_t split_frame_len_ = 0;
  uint32_t split_frame_len_partial_ = 0;
  uint8_t split_frame_padding_start_ = 0;
  // first byte contains bit offset information
  std::array<uint8_t, 1 + 4096> xma_frame_;

  // uint8_t* current_frame_ = nullptr;
  // conversion buffer for 2 channel frame
  std::array<uint8_t, kBytesPerFrameChannel * 2> raw_frame_;
  // std::vector<uint8_t> current_frame_ = std::vector<uint8_t>(0);
};

}  // namespace apu
}  // namespace xe

#endif  // XENIA_APU_XMA_CONTEXT_H_
