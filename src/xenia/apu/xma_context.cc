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
#include "xenia/base/bit_stream.h"
#include "xenia/base/logging.h"
#include "xenia/base/profiling.h"
#include "xenia/base/ring_buffer.h"

extern "C" {
#include "third_party/libav/libavcodec/avcodec.h"
#include "third_party/libav/libavcodec/xma2dec.h"
#include "third_party/libav/libavutil/channel_layout.h"

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

  partial_frame_buffer_.resize(2048);

  // Current frame stuff whatever
  // samples per frame * 2 max channels * output bytes
  current_frame_ = new uint8_t[kSamplesPerFrame * kBytesPerSample * 2];

  // FYI: We're purposely not opening the context here. That is done later.
  return 0;
}

bool XmaContext::Work() {
  std::lock_guard<std::mutex> lock(lock_);
  if (!is_allocated() || !is_enabled()) {
    return false;
  }

  set_is_enabled(false);

  auto context_ptr = memory()->TranslateVirtual(guest_ptr());
  XMA_CONTEXT_DATA data(context_ptr);
  DecodePackets(&data);
  data.Store(context_ptr);
  return true;
}

void XmaContext::Enable() {
  std::lock_guard<std::mutex> lock(lock_);

  auto context_ptr = memory()->TranslateVirtual(guest_ptr());
  XMA_CONTEXT_DATA data(context_ptr);

  XELOGAPU("XmaContext: kicking context %d (buffer %d %d/%d bits)", id(),
           data.current_buffer, data.input_buffer_read_offset,
           (data.current_buffer == 0 ? data.input_buffer_0_packet_count
                                     : data.input_buffer_1_packet_count) *
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
  std::lock_guard<std::mutex> lock(lock_);
  XELOGAPU("XmaContext: reset context %d", id());

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
  std::lock_guard<std::mutex> lock(lock_);
  XELOGAPU("XmaContext: disabling context %d", id());
  set_is_enabled(false);
}

void XmaContext::Release() {
  // Lock it in case the decoder thread is working on it now.
  std::lock_guard<std::mutex> lock(lock_);
  assert_true(is_allocated_ == true);

  set_is_allocated(false);
  auto context_ptr = memory()->TranslateVirtual(guest_ptr());
  std::memset(context_ptr, 0, sizeof(XMA_CONTEXT_DATA));  // Zero it.
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

size_t XmaContext::SavePartial(uint8_t* packet, uint32_t frame_offset_bits,
                               size_t frame_size_bits, bool append) {
  uint8_t* buff = partial_frame_buffer_.data();

  BitStream stream(packet, 2048 * 8);
  stream.SetOffset(frame_offset_bits);

  if (!append) {
    // Reset the buffer.
    // TODO: Probably not necessary.
    std::memset(buff, 0, partial_frame_buffer_.size());

    size_t copy_bits = (2048 * 8) - frame_offset_bits;
    size_t copy_offset = stream.Copy(buff, copy_bits);
    partial_frame_offset_bits_ = copy_bits;
    partial_frame_start_offset_bits_ = copy_offset;

    return copy_bits;
  } else {
    size_t copy_bits = frame_size_bits - partial_frame_offset_bits_;
    size_t copy_offset = stream.Copy(
        buff +
            ((partial_frame_offset_bits_ + partial_frame_start_offset_bits_) /
             8),
        copy_bits);

    partial_frame_offset_bits_ += copy_bits;

    return copy_bits;
  }
}

bool XmaContext::ValidFrameOffset(uint8_t* block, size_t size_bytes,
                                  size_t frame_offset_bits) {
  uint32_t packet_num =
      GetFramePacketNumber(block, size_bytes, frame_offset_bits);
  uint8_t* packet = block + (packet_num * kBytesPerPacket);
  size_t relative_offset_bits = frame_offset_bits % (kBytesPerPacket * 8);

  uint32_t first_frame_offset = xma::GetPacketFrameOffset(packet);
  if (first_frame_offset == -1 || first_frame_offset > kBytesPerPacket * 8) {
    // Packet only contains a partial frame, so no frames can start here.
    return false;
  }

  BitStream stream(packet, kBytesPerPacket * 8);
  stream.SetOffset(first_frame_offset);
  while (true) {
    if (stream.offset_bits() == relative_offset_bits) {
      return true;
    }

    if (stream.BitsRemaining() < 15) {
      // Not enough room for another frame header.
      return false;
    }

    uint64_t size = stream.Read(15);
    if ((size - 15) > stream.BitsRemaining()) {
      // Last frame.
      return false;
    } else if (size == 0x7FFF) {
      // Invalid frame (and last of this packet)
      return false;
    }

    stream.Advance(size - 16);

    // Read the trailing bit to see if frames follow
    if (stream.Read(1) == 0) {
      break;
    }
  }

  return false;
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
  // They can and usually will span packets.

  // Sample rates (data.sample_rate):
  // 0 - 24 kHz
  // 1 - 32 kHz
  // 2 - 44.1 kHz
  // 3 - 48 kHz

  // SPUs also support stereo decoding. (data.is_stereo)

  // Check the output buffer - we cannot decode anything else if it's
  // unavailable.
  if (!data->output_buffer_valid) {
    return;
  }

  // No available data.
  if (!data->input_buffer_0_valid && !data->input_buffer_1_valid) {
    return;
  }

  // XAudio Loops
  // loop_count:
  //  - XAUDIO2_MAX_LOOP_COUNT = 254
  //  - XAUDIO2_LOOP_INFINITE = 255
  // loop_start/loop_end are bit offsets to a specific frame

  // Translate pointers for future use.
  // Sometimes the game will use rolling input buffers. If they do, we cannot
  // assume they form a complete block! In addition, the buffers DO NOT have
  // to be contiguous!
  uint8_t* in0 = data->input_buffer_0_valid
                     ? memory()->TranslatePhysical(data->input_buffer_0_ptr)
                     : nullptr;
  uint8_t* in1 = data->input_buffer_1_valid
                     ? memory()->TranslatePhysical(data->input_buffer_1_ptr)
                     : nullptr;
  uint8_t* current_input_buffer = data->current_buffer ? in1 : in0;

  XELOGAPU("Processing context %d (offset %d, buffer %d, ptr %.8X)", id(),
           data->input_buffer_read_offset, data->current_buffer,
           current_input_buffer);

  size_t input_buffer_0_size =
      data->input_buffer_0_packet_count * kBytesPerPacket;
  size_t input_buffer_1_size =
      data->input_buffer_1_packet_count * kBytesPerPacket;
  size_t input_total_size = input_buffer_0_size + input_buffer_1_size;

  size_t current_input_size =
      data->current_buffer ? input_buffer_1_size : input_buffer_0_size;
  size_t current_input_packet_count = current_input_size / kBytesPerPacket;

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

  // We can only decode an entire frame and write it out at a time, so
  // don't save any samples.
  size_t output_remaining_bytes = output_rb.write_count();
  output_remaining_bytes -= data->is_stereo ? (output_remaining_bytes % 2048)
                                            : (output_remaining_bytes % 1024);

  // Decode until we can't write any more data.
  while (output_remaining_bytes > 0) {
    int num_channels = data->is_stereo ? 2 : 1;
    if (!data->input_buffer_0_valid && !data->input_buffer_1_valid) {
      // Out of data.
      break;
    }

    if (data->input_buffer_read_offset == 0) {
      // Invalid offset. Go ahead and set it.
      uint32_t offset = xma::GetPacketFrameOffset(current_input_buffer);
      if (offset == -1) {
        // No more frames.
        if (data->current_buffer == 0) {
          data->input_buffer_0_valid = 0;
          data->input_buffer_read_offset = 0;
          data->current_buffer++;
        } else if (data->current_buffer == 1) {
          data->input_buffer_1_valid = 0;
          data->input_buffer_read_offset = 0;
          data->current_buffer--;
        }

        // Die if we have no partial saved.
        if (!partial_frame_saved_) {
          return;
        }
      } else {
        data->input_buffer_read_offset = offset;
      }
    }

    if (!ValidFrameOffset(current_input_buffer, current_input_size,
                          data->input_buffer_read_offset)) {
      XELOGAPU("XmaContext %d: Invalid read offset %d!", id(),
               data->input_buffer_read_offset);
      if (data->current_buffer == 0) {
        data->current_buffer = 1;
        data->input_buffer_0_valid = 0;
      } else if (data->current_buffer == 1) {
        data->current_buffer = 0;
        data->input_buffer_1_valid = 0;
      }

      data->input_buffer_read_offset = 0;
      return;
    }

    // Check if we need to save a partial frame.
    if (data->input_buffer_read_offset != 0 && !partial_frame_saved_ &&
        GetFramePacketNumber(current_input_buffer, current_input_size,
                             data->input_buffer_read_offset) ==
            current_input_packet_count - 1) {
      BitStream stream(current_input_buffer, current_input_size * 8);
      stream.SetOffset(data->input_buffer_read_offset);

      if (stream.BitsRemaining() >= 15) {
        uint64_t frame_size = stream.Read(15);
        if (data->input_buffer_read_offset + frame_size >=
                current_input_size * 8 &&
            frame_size != 0x7FFF) {
          uint32_t rel_offset = data->input_buffer_read_offset % (2048 * 8);

          // Frame is cut off! Save and exit.
          partial_frame_saved_ = true;
          partial_frame_size_known_ = true;
          partial_frame_total_size_bits_ = frame_size;
          SavePartial(
              current_input_buffer + (current_input_packet_count - 1) * 2048,
              rel_offset, frame_size, false);
        }
      } else {
        // Header cut in half :/
        uint32_t rel_offset = data->input_buffer_read_offset % (2048 * 8);

        partial_frame_saved_ = true;
        partial_frame_size_known_ = false;
        SavePartial(
            current_input_buffer + (current_input_packet_count - 1) * 2048,
            rel_offset, 0, false);
      }

      if (partial_frame_saved_) {
        XELOGAPU("XmaContext %d: saved a partial frame", id());

        if (data->current_buffer == 0) {
          data->input_buffer_0_valid = 0;
          data->input_buffer_read_offset = 0;
          data->current_buffer++;
        } else if (data->current_buffer == 1) {
          data->input_buffer_1_valid = 0;
          data->input_buffer_read_offset = 0;
          data->current_buffer--;
        }

        return;
      }
    }

    if (partial_frame_saved_ && !partial_frame_size_known_) {
      // Append the rest of the header.
      size_t offset = SavePartial(current_input_buffer, 32, 15, true);

      // Read the frame size.
      BitStream stream(partial_frame_buffer_.data(),
                       15 + partial_frame_start_offset_bits_);
      stream.SetOffset(partial_frame_start_offset_bits_);

      uint64_t size = stream.Read(15);
      partial_frame_size_known_ = true;
      partial_frame_total_size_bits_ = size;

      // Now append the rest of the frame.
      SavePartial(current_input_buffer, 32 + (uint32_t)offset, size, true);
    } else if (partial_frame_saved_) {
      // Append the rest of the frame.
      SavePartial(current_input_buffer, 32, partial_frame_total_size_bits_,
                  true);
    }

    // Prepare the decoder. Reinitialize if any parameters have changed.
    PrepareDecoder(current_input_buffer, current_input_size, data->sample_rate,
                   num_channels);

    bool partial = false;
    size_t bit_offset = data->input_buffer_read_offset;
    if (partial_frame_saved_) {
      XELOGAPU("XmaContext %d: processing saved partial frame", id());
      packet_->data = partial_frame_buffer_.data();
      packet_->size = (int)partial_frame_buffer_.size();

      bit_offset = partial_frame_start_offset_bits_;
      partial = true;
      partial_frame_saved_ = false;
    } else {
      packet_->data = current_input_buffer;
      packet_->size = (int)current_input_size;
    }

    int invalid_frame = 0;  // invalid frame?
    int got_frame = 0;      // successfully decoded a frame?
    int frame_size = 0;
    int len =
        xma2_decode_frame(context_, packet_, decoded_frame_, &got_frame,
                          &invalid_frame, &frame_size, !partial, bit_offset);
    if (!partial && len == 0) {
      // Got the last frame of a packet. Advance the read offset to the next
      // packet.
      uint32_t packet_number =
          GetFramePacketNumber(current_input_buffer, current_input_size,
                               data->input_buffer_read_offset);
      if (packet_number == current_input_packet_count - 1) {
        // Last packet.
        if (data->current_buffer == 0) {
          data->input_buffer_0_valid = 0;
          data->input_buffer_read_offset = 0;
          data->current_buffer = 1;
        } else if (data->current_buffer == 1) {
          data->input_buffer_1_valid = 0;
          data->input_buffer_read_offset = 0;
          data->current_buffer = 0;
        }
      } else {
        // Advance the read offset.
        packet_number++;
        uint8_t* packet = current_input_buffer + (packet_number * 2048);
        uint32_t first_frame_offset = xma::GetPacketFrameOffset(packet);
        if (first_frame_offset == -1) {
          // Invalid packet (only contained a frame partial). Out of input.
          if (data->current_buffer == 0) {
            data->input_buffer_0_valid = 0;
            data->current_buffer = 1;
          } else if (data->current_buffer == 1) {
            data->input_buffer_1_valid = 0;
            data->current_buffer = 0;
          }

          data->input_buffer_read_offset = 0;
        } else {
          data->input_buffer_read_offset =
              packet_number * 2048 * 8 + first_frame_offset;
        }
      }
    }

    if (got_frame) {
      // Valid frame.
      // Check and see if we need to loop back to any spot.
      if (data->loop_count > 0 &&
          data->input_buffer_read_offset == data->loop_end) {
        // Loop back to the beginning.
        data->input_buffer_read_offset = data->loop_start;
        if (data->loop_count < 255) {
          data->loop_count--;
        }
      } else if (!partial && len > 0) {
        data->input_buffer_read_offset += len;
      }
    } else if (len < 0) {
      // Did not get frame
      XELOGAPU("libav failed to decode a frame!");
      if (frame_size && frame_size != 0x7FFF) {
        data->input_buffer_read_offset += frame_size;
      } else {
        data->input_buffer_0_valid = 0;
        data->input_buffer_1_valid = 0;
      }
      return;
    }

    if (got_frame) {
      // Successfully decoded a frame.
      // Copy to the output buffer.
      size_t written_bytes = 0;

      // Validity checks.
      assert(decoded_frame_->nb_samples <= kSamplesPerFrame);
      assert(context_->sample_fmt == AV_SAMPLE_FMT_FLTP);

      // Check the returned buffer size.
      assert(av_samples_get_buffer_size(NULL, context_->channels,
                                        decoded_frame_->nb_samples,
                                        context_->sample_fmt, 1) ==
             context_->channels * decoded_frame_->nb_samples * sizeof(float));

      // Convert the frame.
      ConvertFrame((const uint8_t**)decoded_frame_->data, context_->channels,
                   decoded_frame_->nb_samples, current_frame_);

      assert_true(output_remaining_bytes >= kBytesPerFrame * num_channels);
      output_rb.Write(current_frame_, kBytesPerFrame * num_channels);
      written_bytes = kBytesPerFrame * num_channels;

      output_remaining_bytes -= written_bytes;
      data->output_buffer_write_offset = output_rb.write_offset() / 256;
    }
  }

  // The game will kick us again with a new output buffer later.
  // It's important that we only invalidate this if we actually wrote to it!!
  if (output_rb.write_offset() == output_rb.read_offset()) {
    data->output_buffer_valid = 0;
  }
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
  // Sanity check: Packet metadata is always 1 for XMA2/0 for XMA
  assert_true((block[2] & 0x7) == 1 || (block[2] & 0x7) == 0);

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

bool XmaContext::ConvertFrame(const uint8_t** samples, int num_channels,
                              int num_samples, uint8_t* output_buffer) {
  // Loop through every sample, convert and drop it into the output array.
  // If more than one channel, we need to interleave the samples from each
  // channel next to each other.
  // TODO: This can definitely be optimized with AVX/SSE intrinsics!
  uint32_t o = 0;
  for (int i = 0; i < num_samples; i++) {
    for (int j = 0; j < num_channels; j++) {
      // Select the appropriate array based on the current channel.
      auto sample_array = reinterpret_cast<const float*>(samples[j]);

      // Raw sample should be within [-1, 1].
      // Clamp it, just in case.
      float raw_sample = xe::saturate(sample_array[i]);

      // Convert the sample and output it in big endian.
      float scaled_sample = raw_sample * ((1 << 15) - 1);
      int sample = static_cast<int>(scaled_sample);
      xe::store_and_swap<uint16_t>(&output_buffer[o++ * 2], sample & 0xFFFF);
    }
  }

  return true;
}

}  // namespace apu
}  // namespace xe
