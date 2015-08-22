/**
******************************************************************************
* Xenia : Xbox 360 Emulator Research Project                                 *
******************************************************************************
* Copyright 2015 Ben Vanik. All rights reserved.                             *
* Released under the BSD license - see LICENSE in the root for more details. *
******************************************************************************
*/

#include "xenia/apu/xma_context.h"

#include <algorithm>
#include <cstring>

#include "xenia/apu/xma_decoder.h"
#include "xenia/apu/xma_helpers.h"
#include "xenia/base/logging.h"
#include "xenia/base/ring_buffer.h"
#include "xenia/profiling.h"

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavcodec/xma2dec.h"
#include "libavutil/channel_layout.h"

extern AVCodec ff_xma2_decoder;
}  // extern "C"

// Credits for most of this code goes to:
// https://github.com/koolkdev/libertyv/blob/master/libav_wrapper/xma2dec.c

namespace xe {
namespace apu {

XmaContext::XmaContext() = default;

XmaContext::~XmaContext() {
  if (context_) {
    if (avcodec_is_open(context_)) {
      avcodec_close(context_);
    }
    av_free(context_);
  }
  if (decoded_frame_) {
    av_frame_free(&decoded_frame_);
  }
  if (current_frame_) {
    delete[] current_frame_;
  }
}

int XmaContext::Setup(uint32_t id, Memory* memory, uint32_t guest_ptr) {
  id_ = id;
  memory_ = memory;
  guest_ptr_ = guest_ptr;

  // Allocate important stuff.
  codec_ = &ff_xma2_decoder;
  if (!codec_) {
    return 1;
  }

  context_ = avcodec_alloc_context3(codec_);
  if (!context_) {
    return 1;
  }

  decoded_frame_ = av_frame_alloc();
  if (!decoded_frame_) {
    return 1;
  }

  packet_ = new AVPacket();
  av_init_packet(packet_);

  // Initialize these to 0. They'll actually be set later.
  context_->channels = 0;
  context_->sample_rate = 0;
  context_->block_align = kBytesPerPacket;

  // Extra data passed to the decoder.
  std::memset(&extra_data_, 0, sizeof(extra_data_));
  extra_data_.bits_per_sample = 16;
  extra_data_.channel_mask = AV_CH_FRONT_RIGHT;
  extra_data_.decode_flags = 0x10D6;

  context_->extradata_size = sizeof(extra_data_);
  context_->extradata = reinterpret_cast<uint8_t*>(&extra_data_);

  // Current frame stuff whatever
  // samples per frame * 2 max channels * output bytes
  current_frame_ = new uint8_t[kSamplesPerFrame * kBytesPerSample * 2];
  current_frame_pos_ = 0;
  frame_samples_size_ = 0;

  // FYI: We're purposely not opening the context here. That is done later.
  return 0;
}

void XmaContext::Work() {
  if (!is_allocated() || !is_enabled()) {
    return;
  }

  std::lock_guard<xe::mutex> lock(lock_);
  set_is_enabled(false);

  auto context_ptr = memory()->TranslateVirtual(guest_ptr());
  XMA_CONTEXT_DATA data(context_ptr);
  DecodePackets(&data);
  data.Store(context_ptr);
}

void XmaContext::Enable() {
  std::lock_guard<xe::mutex> lock(lock_);

  auto context_ptr = memory()->TranslateVirtual(guest_ptr());
  XMA_CONTEXT_DATA data(context_ptr);

  XELOGAPU("XmaContext: kicking context %d (%d/%d bits)", id(),
           data.input_buffer_read_offset, (data.input_buffer_0_packet_count +
                                           data.input_buffer_1_packet_count) *
                                              kBytesPerPacket * 8);

  data.Store(context_ptr);

  set_is_enabled(true);
}

bool XmaContext::Block(bool poll) {
  if (!lock_.try_lock()) {
    if (poll) {
      return false;
    }
    lock_.lock();
  }
  lock_.unlock();
  return true;
}

void XmaContext::Clear() {
  std::lock_guard<xe::mutex> lock(lock_);
  XELOGAPU("XmaContext: reset context %d", id());

  DiscardPacket();

  auto context_ptr = memory()->TranslateVirtual(guest_ptr());
  XMA_CONTEXT_DATA data(context_ptr);

  data.input_buffer_0_valid = 0;
  data.input_buffer_1_valid = 0;
  data.output_buffer_valid = 0;

  data.output_buffer_read_offset = 0;
  data.output_buffer_write_offset = 0;

  data.Store(context_ptr);
}

void XmaContext::Disable() {
  std::lock_guard<xe::mutex> lock(lock_);
  XELOGAPU("XmaContext: disabling context %d", id());
  set_is_enabled(false);
}

void XmaContext::Release() {
  // Lock it in case the decoder thread is working on it now.
  std::lock_guard<xe::mutex> lock(lock_);
  assert_true(is_allocated_ == true);

  set_is_allocated(false);
  auto context_ptr = memory()->TranslateVirtual(guest_ptr());
  std::memset(context_ptr, 0, sizeof(XMA_CONTEXT_DATA));  // Zero it.

  DiscardPacket();
}

int XmaContext::GetSampleRate(int id) {
  switch (id) {
    case 0:
      return 24000;
    case 1:
      return 32000;
    case 2:
      return 44100;
    case 3:
      return 48000;
  }
  assert_always();
  return 0;
}

void XmaContext::DecodePackets(XMA_CONTEXT_DATA* data) {
  SCOPE_profile_cpu_f("apu");

  // What I see:
  // XMA outputs 2 bytes per sample
  // 512 samples per frame (128 per subframe)
  // Max output size is data.output_buffer_block_count * 256

  // This decoder is fed packets (max 4095 per buffer)
  // Packets contain "some" frames
  // 32bit header (big endian)

  // Frames are the smallest thing the SPUs can decode.
  // They usually can span packets (libav handles this)

  // Sample rates (data.sample_rate):
  // 0 - 24 kHz ?
  // 1 - 32 kHz
  // 2 - 44.1 kHz ?
  // 3 - 48 kHz ?

  // SPUs also support stereo decoding. (data.is_stereo)

  // Quick die if there's no data.
  if (!data->input_buffer_0_valid && !data->input_buffer_1_valid) {
    return;
  }

  // Check the output buffer - we cannot decode anything else if it's
  // unavailable.
  if (!data->output_buffer_valid) {
    return;
  }

  // XAudio Loops
  // loop_count:
  //  - XAUDIO2_MAX_LOOP_COUNT = 254
  //  - XAUDIO2_LOOP_INFINITE = 255
  // loop_start/loop_end are bit offsets to a specific frame
  //assert_true(data->loop_count == 0);

  // Translate pointers for future use.
  uint8_t* in0 = data->input_buffer_0_valid
                     ? memory()->TranslatePhysical(data->input_buffer_0_ptr)
                     : nullptr;
  uint8_t* in1 = data->input_buffer_1_valid
                     ? memory()->TranslatePhysical(data->input_buffer_1_ptr)
                     : nullptr;

  size_t input_buffer_0_size =
      data->input_buffer_0_packet_count * kBytesPerPacket;
  size_t input_buffer_1_size =
      data->input_buffer_1_packet_count * kBytesPerPacket;

  // Output buffers are in raw PCM samples, 256 bytes per block.
  // Output buffer is a ring buffer. We need to write from the write offset
  // to the read offset.
  uint8_t* output_buffer = memory()->TranslatePhysical(data->output_buffer_ptr);
  uint32_t output_capacity =
      data->output_buffer_block_count * kBytesPerSubframe;
  uint32_t output_read_offset =
      data->output_buffer_read_offset * kBytesPerSubframe;
  uint32_t output_write_offset =
      data->output_buffer_write_offset * kBytesPerSubframe;

  RingBuffer output_rb(output_buffer, output_capacity);
  output_rb.set_read_offset(output_read_offset);
  output_rb.set_write_offset(output_write_offset);

  size_t output_remaining_bytes = output_rb.write_count();

  // Decode until we can't write any more data.
  while (output_remaining_bytes > 0) {
    if (!data->input_buffer_0_valid && !data->input_buffer_1_valid) {
      // Out of data.
      break;
    }

    int num_channels = data->is_stereo ? 2 : 1;

    // Check if we have part of a frame waiting (and the game hasn't jumped
    // around)
    if (current_frame_pos_ &&
        last_input_read_pos_ == data->input_buffer_read_offset) {
      size_t to_write = std::min(
          output_remaining_bytes,
          ((size_t)kBytesPerFrame * num_channels - current_frame_pos_));
      output_rb.Write(current_frame_, to_write);

      current_frame_pos_ += to_write;
      if (current_frame_pos_ >= kBytesPerFrame * num_channels) {
        current_frame_pos_ = 0;
      }

      data->output_buffer_write_offset = output_rb.write_offset() / 256;
      output_remaining_bytes -= to_write;
      continue;
    }

    int block_last_frame = 0;  // last frame in block?
    int got_frame = 0;         // successfully decoded a frame?
    int frame_size = 0;
    packet_->data = in0;
    packet_->size = data->input_buffer_0_packet_count * 2048;
    PrepareDecoder(in0, data->input_buffer_0_packet_count * 2048,
                   data->sample_rate, num_channels);
    int len = xma2_decode_frame(context_, packet_, decoded_frame_, &got_frame,
                                &block_last_frame, &frame_size,
                                data->input_buffer_read_offset);
    if (block_last_frame) {
      data->input_buffer_0_valid = 0;
      data->input_buffer_1_valid = 0;
      data->output_buffer_valid = 0;
      continue;
    }

    if (len == AVERROR_EOF) {
      // Screw this gtfo
      data->input_buffer_0_valid = 0;
      data->input_buffer_1_valid = 0;
      data->output_buffer_valid = 0;

      continue;
    } else if (len < 0 || !got_frame) {
      // Oh no! Skip the frame and hope everything works.
      data->input_buffer_read_offset += frame_size;

      continue;
    }

    XELOGD("LEN: %d (%x)", len, len);

    data->input_buffer_read_offset += len;
    last_input_read_pos_ = data->input_buffer_read_offset;

    // Copy to the output buffer.
    // Successfully decoded a frame.
    size_t written_bytes = 0;
    if (got_frame) {
      // Validity checks.
      if (decoded_frame_->nb_samples > kSamplesPerFrame) {
        return;
      } else if (context_->sample_fmt != AV_SAMPLE_FMT_FLTP) {
        return;
      }

      // Check the returned buffer size.
      if (av_samples_get_buffer_size(NULL, context_->channels,
                                     decoded_frame_->nb_samples,
                                     context_->sample_fmt, 1) !=
          context_->channels * decoded_frame_->nb_samples * sizeof(float)) {
        return;
      }

      // Loop through every sample, convert and drop it into the output array.
      // If more than one channel, the game wants the samples from each channel
      // interleaved next to each other.
      uint32_t o = 0;
      for (int i = 0; i < decoded_frame_->nb_samples; i++) {
        for (int j = 0; j < context_->channels; j++) {
          // Select the appropriate array based on the current channel.
          auto sample_array = reinterpret_cast<float*>(decoded_frame_->data[j]);

          // Raw sample should be within [-1, 1].
          // Clamp it, just in case.
          float raw_sample = xe::saturate(sample_array[i]);

          // Convert the sample and output it in big endian.
          float scaled_sample = raw_sample * ((1 << 15) - 1);
          int sample = static_cast<int>(scaled_sample);
          xe::store_and_swap<uint16_t>(&current_frame_[o++ * 2],
                                       sample & 0xFFFF);
        }
      }
      current_frame_pos_ = 0;

      if (output_remaining_bytes < kBytesPerFrame * num_channels) {
        // Output buffer isn't big enough to store the entire frame! Write out a
        // part of it.
        current_frame_pos_ = output_remaining_bytes;
        output_rb.Write(current_frame_, output_remaining_bytes);

        written_bytes = output_remaining_bytes;
      } else {
        output_rb.Write(current_frame_, kBytesPerFrame * num_channels);

        written_bytes = kBytesPerFrame * num_channels;
      }
    }

    output_remaining_bytes -= written_bytes;
    data->output_buffer_write_offset = output_rb.write_offset() / 256;
  }

  // The game will kick us again with a new output buffer later.
  data->output_buffer_valid = 0;
}

uint32_t XmaContext::GetFramePacketNumber(uint8_t* block, size_t size,
                                          size_t bit_offset) {
  size *= 8;
  if (bit_offset >= size) {
    // Not good :(
    assert_always();
    return -1;
  }

  size_t byte_offset = bit_offset >> 3;
  size_t packet_number = byte_offset / kBytesPerPacket;

  return (uint32_t)packet_number;
}

int XmaContext::PrepareDecoder(uint8_t* block, size_t size, int sample_rate,
                               int channels) {
  // Sanity check: Packet metadata is always 1 for XMA2
  assert_true((block[2] & 0x7) == 1);

  sample_rate = GetSampleRate(sample_rate);

  // Re-initialize the context with new sample rate and channels.
  if (context_->sample_rate != sample_rate || context_->channels != channels) {
    // We have to reopen the codec so it'll realloc whatever data it needs.
    // TODO(DrChat): Find a better way.
    avcodec_close(context_);

    context_->sample_rate = sample_rate;
    context_->channels = channels;
    extra_data_.channel_mask =
        channels == 2 ? AV_CH_LAYOUT_STEREO : AV_CH_LAYOUT_MONO;

    if (avcodec_open2(context_, codec_, NULL) < 0) {
      XELOGE("XmaContext: Failed to reopen libav context");
      return 1;
    }
  }

  av_frame_unref(decoded_frame_);

  return 0;
}

int XmaContext::StartPacket(XMA_CONTEXT_DATA* data) {
  // Translate pointers for future use.
  uint8_t* in0 = data->input_buffer_0_valid
                     ? memory()->TranslatePhysical(data->input_buffer_0_ptr)
                     : nullptr;
  uint8_t* in1 = data->input_buffer_1_valid
                     ? memory()->TranslatePhysical(data->input_buffer_1_ptr)
                     : nullptr;

  int sample_rate = GetSampleRate(data->sample_rate);
  int channels = data->is_stereo ? 2 : 1;

  // See if we've finished with the input.
  // Block count is in packets, so expand by packet size.
  uint32_t input_size_0_bytes = data->input_buffer_0_valid
                                    ? (data->input_buffer_0_packet_count) * 2048
                                    : 0;
  uint32_t input_size_1_bytes = data->input_buffer_1_valid
                                    ? (data->input_buffer_1_packet_count) * 2048
                                    : 0;

  // Total input size
  uint32_t input_size_bytes = input_size_0_bytes + input_size_1_bytes;

  // Calculate the first frame offset we need to decode.
  uint32_t frame_offset_bits = (data->input_buffer_read_offset % (2048 * 8));

  // Input read offset is in bits. Typically starts at 32 (4 bytes).
  // "Sequence" offset - used internally for WMA Pro decoder.
  // Just the read offset.
  // NOTE: Read offset may not be at the first frame in a packet!
  uint32_t packet_offset_bytes = (data->input_buffer_read_offset & ~0x7FF) / 8;
  if (packet_offset_bytes % 2048 != 0) {
    packet_offset_bytes -= packet_offset_bytes % 2048;
  }
  uint32_t input_remaining_bytes = input_size_bytes - packet_offset_bytes;

  if (packet_offset_bytes >= input_size_bytes) {
    // No more data available and no packet prepared.
    return -1;
  }

  // Setup input offset and input buffer.
  uint32_t input_offset_bytes = packet_offset_bytes;
  auto input_buffer = in0;

  if (packet_offset_bytes >= input_size_0_bytes && input_size_1_bytes) {
    // Size overlap, select input buffer 1.
    // TODO(DrChat): This needs testing.
    input_offset_bytes -= input_size_0_bytes;
    input_buffer = in1;
  }

  // Still have data to read.
  auto packet = input_buffer + input_offset_bytes;
  assert_true(input_offset_bytes % 2048 == 0);
  PreparePacket(packet, packet_offset_bytes, kBytesPerPacket, sample_rate,
                channels);

  data->input_buffer_read_offset += kBytesPerPacket * 8;

  input_remaining_bytes -= kBytesPerPacket;
  if (input_remaining_bytes <= 0) {
    // Used the last of the data but prepared a packet.
    return 0;
  }

  return input_remaining_bytes;
}

int XmaContext::PreparePacket(uint8_t* input, size_t seq_offset, size_t size,
                              int sample_rate, int channels) {
  if (size != kBytesPerPacket) {
    // Invalid packet size!
    assert_always();
    return 1;
  }
  if (packet_->size > 0 || current_frame_pos_ != frame_samples_size_) {
    // Haven't finished parsing another packet.
    return 1;
  }

  // Packet metadata is always 1 for XMA2
  assert_true((input[2] & 0x7) == 1);

  packet_->data = input;
  packet_->size = (int)size;

  // Re-initialize the context with new sample rate and channels.
  if (context_->sample_rate != sample_rate || context_->channels != channels) {
    // We have to reopen the codec so it'll realloc whatever data it needs.
    // TODO(DrChat): Find a better way.
    avcodec_close(context_);

    context_->sample_rate = sample_rate;
    context_->channels = channels;
    extra_data_.channel_mask =
        channels == 2 ? AV_CH_LAYOUT_STEREO : AV_CH_LAYOUT_MONO;

    if (avcodec_open2(context_, codec_, NULL) < 0) {
      XELOGE("XmaContext: Failed to reopen libav context");
      return 1;
    }
  }

  return 0;
}

void XmaContext::DiscardPacket() {
  if (packet_->size > 0 || current_frame_pos_ != frame_samples_size_) {
    packet_->data = 0;
    packet_->size = 0;
    current_frame_pos_ = frame_samples_size_;
  }
}

int XmaContext::DecodePacket(uint8_t* output, size_t output_offset,
                             size_t output_size, size_t* read_bytes) {
  size_t to_copy = 0;
  size_t original_offset = output_offset;
  if (read_bytes) {
    *read_bytes = 0;
  }

  // We're holding onto an already-decoded frame. Copy it out.
  if (current_frame_pos_ != frame_samples_size_) {
    to_copy = std::min(output_size, frame_samples_size_ - current_frame_pos_);
    memcpy(output + output_offset, current_frame_ + current_frame_pos_,
           to_copy);

    current_frame_pos_ += to_copy;
    output_size -= to_copy;
    output_offset += to_copy;
  }

  while (output_size > 0 && packet_->size > 0) {
    int got_frame = 0;

    // Decode the current frame.
    int len =
        avcodec_decode_audio4(context_, decoded_frame_, &got_frame, packet_);
    if (len < 0) {
      // Error in codec (bad sample rate or something).
      return len;
    }

    if (read_bytes) {
      *read_bytes += len;
    }

    // Offset by decoded length.
    packet_->size -= len;
    packet_->data += len;
    packet_->dts = packet_->pts = AV_NOPTS_VALUE;

    // Successfully decoded a frame.
    if (got_frame) {
      // Validity checks.
      if (decoded_frame_->nb_samples > kSamplesPerFrame) {
        return -2;
      } else if (context_->sample_fmt != AV_SAMPLE_FMT_FLTP) {
        return -3;
      }

      // Check the returned buffer size.
      if (av_samples_get_buffer_size(NULL, context_->channels,
                                     decoded_frame_->nb_samples,
                                     context_->sample_fmt, 1) !=
          context_->channels * decoded_frame_->nb_samples * sizeof(float)) {
        return -4;
      }

      // Loop through every sample, convert and drop it into the output array.
      // If more than one channel, the game wants the samples from each channel
      // interleaved next to each other.
      uint32_t o = 0;
      for (int i = 0; i < decoded_frame_->nb_samples; i++) {
        for (int j = 0; j < context_->channels; j++) {
          // Select the appropriate array based on the current channel.
          auto sample_array = reinterpret_cast<float*>(decoded_frame_->data[j]);

          // Raw sample should be within [-1, 1].
          // Clamp it, just in case.
          float raw_sample = xe::saturate(sample_array[i]);

          // Convert the sample and output it in big endian.
          float scaled_sample = raw_sample * ((1 << 15) - 1);
          int sample = static_cast<int>(scaled_sample);
          xe::store_and_swap<uint16_t>(&current_frame_[o++ * 2],
                                       sample & 0xFFFF);
        }
      }
      current_frame_pos_ = 0;

      // Total size of the frame's samples.
      // Magic number 2 is sizeof an output sample.
      frame_samples_size_ = context_->channels * decoded_frame_->nb_samples * 2;

      to_copy = std::min(output_size, (size_t)(frame_samples_size_));
      std::memcpy(output + output_offset, current_frame_, to_copy);

      current_frame_pos_ += to_copy;
      output_size -= to_copy;
      output_offset += to_copy;
    }
  }

  // Return number of bytes written.
  return static_cast<int>(output_offset - original_offset);
}

}  // namespace apu
}  // namespace xe
