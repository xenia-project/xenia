/**
******************************************************************************
* Xenia : Xbox 360 Emulator Research Project                                 *
******************************************************************************
* Copyright 2024 Xenia Canary. All rights reserved.                          *
* Released under the BSD license - see LICENSE in the root for more details. *
******************************************************************************
*/

#include "xenia/apu/xma_context_new.h"
#include "xenia/apu/xma_helpers.h"

#include <algorithm>

#include "xenia/base/logging.h"
#include "xenia/base/platform.h"
#include "xenia/base/profiling.h"

extern "C" {
#if XE_COMPILER_MSVC
#pragma warning(push)
#pragma warning(disable : 4101 4244 5033)
#endif
#include "third_party/FFmpeg/libavcodec/avcodec.h"
#if XE_COMPILER_MSVC
#pragma warning(pop)
#endif
}  // extern "C"

// Credits for most of this code goes to:
// https://github.com/koolkdev/libertyv/blob/master/libav_wrapper/xma2dec.c

namespace xe {
namespace apu {

XmaContextNew::XmaContextNew() = default;

XmaContextNew::~XmaContextNew() {
  if (av_context_) {
    if (avcodec_is_open(av_context_)) {
      avcodec_close(av_context_);
    }
    av_free(av_context_);
  }
  if (av_frame_) {
    av_frame_free(&av_frame_);
  }
}

int XmaContextNew::Setup(uint32_t id, Memory* memory, uint32_t guest_ptr) {
  id_ = id;
  memory_ = memory;
  guest_ptr_ = guest_ptr;

  // Allocate ffmpeg stuff:
  av_packet_ = av_packet_alloc();
  assert_not_null(av_packet_);
  av_packet_->buf = av_buffer_alloc(128 * 1024);

  // find the XMA2 audio decoder
  av_codec_ = avcodec_find_decoder(AV_CODEC_ID_XMAFRAMES);
  if (!av_codec_) {
    XELOGE("XmaContext {}: Codec not found", id);
    return 1;
  }

  av_context_ = avcodec_alloc_context3(av_codec_);
  if (!av_context_) {
    XELOGE("XmaContext {}: Couldn't allocate context", id);
    return 1;
  }

  // Initialize these to 0. They'll actually be set later.
  av_context_->channels = 0;
  av_context_->sample_rate = 0;

  av_frame_ = av_frame_alloc();
  if (!av_frame_) {
    XELOGE("XmaContext {}: Couldn't allocate frame", id);
    return 1;
  }

  // FYI: We're purposely not opening the codec here. That is done later.
  return 0;
}

RingBuffer XmaContextNew::PrepareOutputRingBuffer(XMA_CONTEXT_DATA* data) {
  const uint32_t output_capacity =
      data->output_buffer_block_count * kOutputBytesPerBlock;
  const uint32_t output_read_offset =
      data->output_buffer_read_offset * kOutputBytesPerBlock;
  const uint32_t output_write_offset =
      data->output_buffer_write_offset * kOutputBytesPerBlock;

  if (output_capacity > kOutputMaxSizeBytes) {
    XELOGW(
        "XmaContext {}: Output buffer uses more space than expected! "
        "(Actual: {} Max: {})",
        id(), output_capacity, kOutputMaxSizeBytes);
  }

  uint8_t* output_buffer = memory()->TranslatePhysical(data->output_buffer_ptr);

  // Output buffers are in raw PCM samples, 256 bytes per block.
  // Output buffer is a ring buffer. We need to write from the write offset
  // to the read offset.
  RingBuffer output_rb(output_buffer, output_capacity);
  output_rb.set_read_offset(output_read_offset);
  output_rb.set_write_offset(output_write_offset);
  remaining_subframe_blocks_in_output_buffer_ =
      (int32_t)output_rb.write_count() / kOutputBytesPerBlock;

  return output_rb;
}

bool XmaContextNew::Work() {
  if (!is_enabled() || !is_allocated()) {
    return false;
  }

  std::lock_guard<xe_mutex> lock(lock_);
  set_is_enabled(false);

  auto context_ptr = memory()->TranslateVirtual(guest_ptr());
  XMA_CONTEXT_DATA data(context_ptr);

  if (!data.output_buffer_valid) {
    return true;
  }

  RingBuffer output_rb = PrepareOutputRingBuffer(&data);

  const int32_t minimum_subframe_decode_count =
      (data.subframe_decode_count * 2) - 1;

  // We don't have enough space to even make one pass
  // Waiting for decoder to return more space.
  if (minimum_subframe_decode_count >
      remaining_subframe_blocks_in_output_buffer_) {
    XELOGD("XmaContext {}: No space for subframe decoding {}/{}!", id(),
           minimum_subframe_decode_count,
           remaining_subframe_blocks_in_output_buffer_);
    data.Store(context_ptr);
    return true;
  }

  while (remaining_subframe_blocks_in_output_buffer_ >=
         minimum_subframe_decode_count) {
    XELOGAPU(
        "XmaContext {}: Write Count: {}, Capacity: {} - {} {} Subframes: {} "
        "Skip: {}",
        id(), (uint32_t)output_rb.write_count(),
        remaining_subframe_blocks_in_output_buffer_,
        data.input_buffer_0_valid + (data.input_buffer_1_valid << 1),
        data.output_buffer_valid, data.subframe_decode_count,
        data.subframe_skip_count);

    Decode(&data);
    Consume(&output_rb, &data);

    if (!data.IsAnyInputBufferValid() || data.error_status == 4) {
      break;
    }
  }

  data.output_buffer_write_offset =
      output_rb.write_offset() / kOutputBytesPerBlock;

  XELOGAPU("XmaContext {}: Read Output: {} Write Output: {}", id(),
           data.output_buffer_read_offset, data.output_buffer_write_offset);

  // That's a bit misleading due to nature of ringbuffer
  // when write and read offset matches it might mean that we wrote nothing
  // or we fully saturated allocated space.
  if (output_rb.empty()) {
    data.output_buffer_valid = 0;
  }

  // TODO: Rewrite!
  // There is a case when game can modify certain parts of context mid-play
  // and decoder should be aware of it
  data.Store(context_ptr);
  return true;
}

void XmaContextNew::Enable() {
  std::lock_guard<xe_mutex> lock(lock_);

  auto context_ptr = memory()->TranslateVirtual(guest_ptr());
  XMA_CONTEXT_DATA data(context_ptr);

  XELOGAPU("XmaContext: kicking context {} (buffer {} {}/{} bits)", id(),
           data.current_buffer, data.input_buffer_read_offset,
           data.GetCurrentInputBufferPacketCount() * kBitsPerPacket);

  data.Store(context_ptr);
  set_is_enabled(true);
}

bool XmaContextNew::Block(bool poll) {
  if (!lock_.try_lock()) {
    if (poll) {
      return false;
    }
    lock_.lock();
  }
  lock_.unlock();
  return true;
}

void XmaContextNew::Clear() {
  std::lock_guard<xe_mutex> lock(lock_);
  XELOGAPU("XmaContext: reset context {}", id());

  auto context_ptr = memory()->TranslateVirtual(guest_ptr());
  XMA_CONTEXT_DATA data(context_ptr);

  data.input_buffer_0_valid = 0;
  data.input_buffer_1_valid = 0;
  data.output_buffer_valid = 0;

  data.input_buffer_read_offset = 0;
  data.output_buffer_read_offset = 0;
  data.output_buffer_write_offset = 0;
  data.input_buffer_read_offset = kBitsPerPacketHeader;

  current_frame_remaining_subframes_ = 0;
  data.Store(context_ptr);
}

void XmaContextNew::Disable() {
  std::lock_guard<xe_mutex> lock(lock_);
  XELOGAPU("XmaContext: disabling context {}", id());
  set_is_enabled(false);
}

void XmaContextNew::Release() {
  // Lock it in case the decoder thread is working on it now.
  std::lock_guard<xe_mutex> lock(lock_);
  assert_true(is_allocated_ == true);

  set_is_allocated(false);
  auto context_ptr = memory()->TranslateVirtual(guest_ptr());
  std::memset(context_ptr, 0, sizeof(XMA_CONTEXT_DATA));  // Zero it.
}

int XmaContextNew::GetSampleRate(int id) {
  return kIdToSampleRate[std::min(id, 3)];
}

void XmaContextNew::SwapInputBuffer(XMA_CONTEXT_DATA* data) {
  // No more frames.
  if (data->current_buffer == 0) {
    data->input_buffer_0_valid = 0;
  } else {
    data->input_buffer_1_valid = 0;
  }
  data->current_buffer ^= 1;
  data->input_buffer_read_offset = kBitsPerPacketHeader;
}

void XmaContextNew::Consume(RingBuffer* output_rb, XMA_CONTEXT_DATA* data) {
  if (!current_frame_remaining_subframes_) {
    return;
  }

  const int8_t subframes_to_write =
      std::min((int8_t)current_frame_remaining_subframes_,
               (int8_t)data->subframe_decode_count);

  const int8_t raw_frame_read_offset =
      ((kBytesPerFrameChannel / kOutputBytesPerBlock) << data->is_stereo) -
      current_frame_remaining_subframes_;
  // + data->subframe_skip_count;

  output_rb->Write(
      raw_frame_.data() + (kOutputBytesPerBlock * raw_frame_read_offset),
      subframes_to_write * kOutputBytesPerBlock);
  remaining_subframe_blocks_in_output_buffer_ -= subframes_to_write;
  current_frame_remaining_subframes_ -= subframes_to_write;

  XELOGAPU("XmaContext {}: Consume: {} - {} - {} - {} - {}", id(),
           remaining_subframe_blocks_in_output_buffer_,
           data->output_buffer_write_offset, data->output_buffer_read_offset,
           output_rb->write_offset(), current_frame_remaining_subframes_);
}

void XmaContextNew::Decode(XMA_CONTEXT_DATA* data) {
  SCOPE_profile_cpu_f("apu");

  // No available data.
  if (!data->IsAnyInputBufferValid()) {
    // data->error_status = 4;
    return;
  }

  if (current_frame_remaining_subframes_ > 0) {
    return;
  }

  uint8_t* current_input_buffer = GetCurrentInputBuffer(data);

  if (!data->IsCurrentInputBufferValid()) {
    XELOGE(
        "XmaContext {}: Invalid current buffer! Selected Buffer: {} Valid: {} "
        "Pointer: {:08X}",
        id(), data->current_buffer, data->IsCurrentInputBufferValid(),
        data->GetCurrentInputBufferAddress());
    return;
  }

  input_buffer_.fill(0);

  UpdateLoopStatus(data);

  if (!data->output_buffer_block_count) {
    XELOGE("XmaContext {}: Error - Received 0 for output_buffer_block_count!",
           id());
    return;
  }

  XELOGAPU(
      "Processing context {} (offset {}, buffer {}, ptr {:p}, output buffer "
      "{:08X}, output buffer count {})",
      id(), data->input_buffer_read_offset, data->current_buffer,
      static_cast<void*>(current_input_buffer), data->output_buffer_ptr,
      data->output_buffer_block_count);

  const uint32_t current_input_size = GetCurrentInputBufferSize(data);
  const uint32_t current_input_packet_count =
      current_input_size / kBytesPerPacket;

  const int16_t packet_index =
      GetPacketNumber(current_input_size, data->input_buffer_read_offset);

  if (packet_index == -1) {
    XELOGE("XmaContext {}: Invalid packet index. Input read offset: {}", id(),
           data->input_buffer_read_offset);
    return;
  }

  uint8_t* packet = current_input_buffer + (packet_index * kBytesPerPacket);
  // Because game can reset read offset. We must assure that new offset is
  // valid. Splitted frames aren't handled here, so it's not a big deal.
  const uint32_t frame_offset = xma::GetPacketFrameOffset(packet);
  if (data->input_buffer_read_offset < frame_offset) {
    data->input_buffer_read_offset = frame_offset;
  }

  const uint32_t relative_offset =
      data->input_buffer_read_offset % kBitsPerPacket;
  const kPacketInfo packet_info = GetPacketInfo(packet, relative_offset);
  const uint32_t packet_to_skip = xma::GetPacketSkipCount(packet) + 1;
  const uint32_t next_packet_index = packet_index + packet_to_skip;

  BitStream stream =
      BitStream(current_input_buffer, (packet_index + 1) * kBitsPerPacket);
  stream.SetOffset(data->input_buffer_read_offset);

  const uint64_t bits_to_copy = GetAmountOfBitsToRead(
      (uint32_t)stream.BitsRemaining(), packet_info.current_frame_size_);

  if (bits_to_copy == 0) {
    XELOGE("XmaContext {}: There is no bits to copy!", id());
    SwapInputBuffer(data);
    return;
  }

  if (packet_info.isLastFrameInPacket()) {
    // Frame is a splitted frame
    if (stream.BitsRemaining() < packet_info.current_frame_size_) {
      const uint8_t* next_packet =
          GetNextPacket(data, next_packet_index, current_input_packet_count);

      if (!next_packet) {
        // Error path
        // Decoder probably should return error here
        // Not sure what error code should be returned
        data->error_status = 4;
        return;
      }
      // Copy next packet to buffer
      std::memcpy(input_buffer_.data() + kBytesPerPacketData,
                  next_packet + kBytesPerPacketHeader, kBytesPerPacketData);
    }
  }

  // Copy current packet to buffer
  std::memcpy(input_buffer_.data(), packet + kBytesPerPacketHeader,
              kBytesPerPacketData);

  stream = BitStream(input_buffer_.data(),
                     (kBitsPerPacket - kBitsPerPacketHeader) * 2);
  stream.SetOffset(relative_offset - kBitsPerPacketHeader);

  xma_frame_.fill(0);

  XELOGAPU(
      "XmaContext {}: Reading Frame {}/{} (size: {}) From Packet "
      "{}/{}",
      id(), (int32_t)packet_info.current_frame_, packet_info.frame_count_,
      packet_info.current_frame_size_, packet_index,
      current_input_packet_count);

  const uint32_t padding_start = static_cast<uint8_t>(
      stream.Copy(xma_frame_.data() + 1, packet_info.current_frame_size_));

  raw_frame_.fill(0);

  PrepareDecoder(data->sample_rate, bool(data->is_stereo));
  PreparePacket(packet_info.current_frame_size_, padding_start);
  if (DecodePacket(av_context_, av_packet_, av_frame_)) {
    // dump_raw(av_frame_, id());
    ConvertFrame(reinterpret_cast<const uint8_t**>(&av_frame_->data),
                 bool(data->is_stereo), raw_frame_.data());
  }

  // TODO: Write function to regenerate decoder
  // TODO: Be aware of subframe_skips & loops subframes skips
  current_frame_remaining_subframes_ = 4 << data->is_stereo;

  // Compute where to go next.
  if (!packet_info.isLastFrameInPacket()) {
    const uint32_t next_frame_offset =
        (data->input_buffer_read_offset + bits_to_copy) % kBitsPerPacket;

    XELOGAPU("XmaContext {}: Index: {}/{} - Next frame offset: {}", id(),
             (int32_t)packet_info.current_frame_, packet_info.frame_count_,
             next_frame_offset);

    data->input_buffer_read_offset =
        (packet_index * kBitsPerPacket) + next_frame_offset;
    return;
  }

  const uint8_t* next_packet =
      GetNextPacket(data, next_packet_index, current_input_packet_count);

  if (!next_packet) {
    // Error path
    // Decoder probably should return error here
    // Not sure what error code should be returned
    // data->error_status = 4;
    // data->output_buffer_valid = 0;
    // return;
  }

  uint32_t next_input_offset = GetNextPacketReadOffset(
      current_input_buffer, next_packet_index, current_input_packet_count);

  if (next_input_offset == kBitsPerPacketHeader) {
    SwapInputBuffer(data);
    // We're at start of next buffer
    // If it have any frame in this packet decoder should go to first frame in
    // packet If it doesn't have any frame then it should immediatelly go to
    // next packet
    if (data->IsAnyInputBufferValid()) {
      next_input_offset = xma::GetPacketFrameOffset(
          memory()->TranslatePhysical(data->GetCurrentInputBufferAddress()));

      if (next_input_offset > kMaxFrameSizeinBits) {
        XELOGAPU(
            "XmaContext {}: Next buffer contains no frames in packet! Frame "
            "offset: {}",
            id(), next_input_offset);
        SwapInputBuffer(data);
        return;
      }
      XELOGAPU("XmaContext {}: Next buffer first frame starts at: {}", id(),
               next_input_offset);
    }
  }
  data->input_buffer_read_offset = next_input_offset;
  return;
}

//  Frame & Packet searching methods

void XmaContextNew::UpdateLoopStatus(XMA_CONTEXT_DATA* data) {
  if (data->loop_count == 0) {
    return;
  }

  const uint32_t loop_start = std::max(kBitsPerPacketHeader, data->loop_start);
  const uint32_t loop_end = std::max(kBitsPerPacketHeader, data->loop_end);

  XELOGAPU("XmaContext {}: Looped Data: {} < {} (Start: {}) Remaining: {}",
           id(), data->input_buffer_read_offset, data->loop_end,
           data->loop_start, data->loop_count);

  if (data->input_buffer_read_offset != loop_end) {
    return;
  }

  data->input_buffer_read_offset = loop_start;

  if (data->loop_count != 255) {
    data->loop_count--;
  }
}

const uint8_t* XmaContextNew::GetNextPacket(
    XMA_CONTEXT_DATA* data, uint32_t next_packet_index,
    uint32_t current_input_packet_count) {
  if (next_packet_index < current_input_packet_count) {
    return memory()->TranslatePhysical(data->GetCurrentInputBufferAddress()) +
           next_packet_index * kBytesPerPacket;
  }

  const uint8_t next_buffer_index = data->current_buffer ^ 1;

  if (!data->IsInputBufferValid(next_buffer_index)) {
    return nullptr;
  }

  const uint32_t next_buffer_address =
      data->GetInputBufferAddress(next_buffer_index);

  if (!next_buffer_address) {
    // This should never occur but there is always a chance
    XELOGE(
        "XmaContext {}: Buffer is marked as valid, but doesn't have valid "
        "pointer!",
        id());
    return nullptr;
  }

  return memory()->TranslatePhysical(next_buffer_address);
}

const uint32_t XmaContextNew::GetNextPacketReadOffset(
    uint8_t* buffer, uint32_t next_packet_index,
    uint32_t current_input_packet_count) {
  if (next_packet_index >= current_input_packet_count) {
    return kBitsPerPacketHeader;
  }

  uint8_t* next_packet = buffer + (next_packet_index * kBytesPerPacket);
  const uint32_t packet_frame_offset = xma::GetPacketFrameOffset(next_packet);

  if (packet_frame_offset > kMaxFrameSizeinBits) {
    const uint32_t offset = GetNextPacketReadOffset(
        buffer, next_packet_index + 1, current_input_packet_count);
    return offset;
  }

  const uint32_t new_input_buffer_offset =
      (next_packet_index * kBitsPerPacket) + packet_frame_offset;

  XELOGAPU("XmaContext {}: new offset: {} packet_offset: {} packet: {}/{}",
           id(), new_input_buffer_offset, packet_frame_offset,
           next_packet_index, current_input_packet_count);
  return new_input_buffer_offset;
}

const uint32_t XmaContextNew::GetAmountOfBitsToRead(
    const uint32_t remaining_stream_bits, const uint32_t frame_size) {
  return std::min(remaining_stream_bits, frame_size);
}

uint32_t XmaContextNew::GetCurrentInputBufferSize(XMA_CONTEXT_DATA* data) {
  return data->GetCurrentInputBufferPacketCount() * kBytesPerPacket;
}

uint8_t* XmaContextNew::GetCurrentInputBuffer(XMA_CONTEXT_DATA* data) {
  return memory()->TranslatePhysical(data->GetCurrentInputBufferAddress());
}

const kPacketInfo XmaContextNew::GetPacketInfo(uint8_t* packet,
                                               uint32_t frame_offset) {
  kPacketInfo packet_info = {};

  const uint32_t first_frame_offset = xma::GetPacketFrameOffset(packet);
  BitStream stream(packet, kBitsPerPacket);
  stream.SetOffset(first_frame_offset);

  // Handling of splitted frame
  if (frame_offset < first_frame_offset) {
    packet_info.current_frame_ = 0;
    packet_info.current_frame_size_ = first_frame_offset - frame_offset;
  }

  while (true) {
    if (stream.BitsRemaining() < kBitsPerFrameHeader) {
      break;
    }

    const uint64_t frame_size = stream.Peek(kBitsPerFrameHeader);
    if (frame_size == xma::kMaxFrameLength) {
      break;
    }

    if (stream.offset_bits() == frame_offset) {
      packet_info.current_frame_ = packet_info.frame_count_;
      packet_info.current_frame_size_ = (uint32_t)frame_size;
    }

    packet_info.frame_count_++;

    if (frame_size > stream.BitsRemaining()) {
      // Last frame.
      break;
    }

    stream.Advance(frame_size - 1);

    // Read the trailing bit to see if frames follow
    if (stream.Read(1) == 0) {
      break;
    }
  }

  if (xma::IsPacketXma2Type(packet)) {
    const uint8_t xma2_frame_count = xma::GetPacketFrameCount(packet);
    if (xma2_frame_count != packet_info.frame_count_) {
      XELOGE(
          "XmaContext {}: XMA2 packet header defines different amount of "
          "frames than internally found! (Header: {} Found: {})",
          id(), xma2_frame_count, packet_info.frame_count_);
    }
  }
  return packet_info;
}

int16_t XmaContextNew::GetPacketNumber(size_t size, size_t bit_offset) {
  if (bit_offset < kBitsPerPacketHeader) {
    assert_always();
    return -1;
  }

  if (bit_offset >= (size << 3)) {
    assert_always();
    return -1;
  }

  size_t byte_offset = bit_offset >> 3;
  size_t packet_number = byte_offset / kBytesPerPacket;

  return (int16_t)packet_number;
}

int XmaContextNew::PrepareDecoder(int sample_rate, bool is_two_channel) {
  sample_rate = GetSampleRate(sample_rate);

  // Re-initialize the context with new sample rate and channels.
  uint32_t channels = is_two_channel ? 2 : 1;
  if (av_context_->sample_rate != sample_rate ||
      av_context_->channels != channels) {
    // We have to reopen the codec so it'll realloc whatever data it needs.
    // TODO(DrChat): Find a better way.
    avcodec_close(av_context_);

    av_context_->sample_rate = sample_rate;
    av_context_->channels = channels;

    if (avcodec_open2(av_context_, av_codec_, NULL) < 0) {
      XELOGE("XmaContext: Failed to reopen FFmpeg context");
      return -1;
    }
    return 1;
  }
  return 0;
}

void XmaContextNew::PreparePacket(const uint32_t frame_size,
                                  const uint32_t frame_padding) {
  av_packet_->data = xma_frame_.data();
  av_packet_->size =
      static_cast<int>(1 + ((frame_padding + frame_size) / 8) +
                       (((frame_padding + frame_size) % 8) ? 1 : 0));

  auto padding_end = av_packet_->size * 8 - (8 + frame_padding + frame_size);
  assert_true(padding_end < 8);
  xma_frame_[0] = ((frame_padding & 7) << 5) | ((padding_end & 7) << 2);
}

bool XmaContextNew::DecodePacket(AVCodecContext* av_context,
                                 const AVPacket* av_packet, AVFrame* av_frame) {
  auto ret = avcodec_send_packet(av_context, av_packet);
  if (ret < 0) {
    XELOGE("XmaContext {}: Error sending packet for decoding", id());
    return false;
  }
  ret = avcodec_receive_frame(av_context, av_frame);

  if (ret < 0) {
    XELOGE("XmaContext {}: Error during decoding", id());
    return false;
  }
  return true;
}

}  // namespace apu
}  // namespace xe