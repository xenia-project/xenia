/**
******************************************************************************
* Xenia : Xbox 360 Emulator Research Project                                 *
******************************************************************************
* Copyright 2015 Ben Vanik. All rights reserved.                             *
* Released under the BSD license - see LICENSE in the root for more details. *
******************************************************************************
*/

#include "xenia/apu/xma_context.h"
#include "xenia/apu/xma_decoder.h"
#include "xenia/base/logging.h"
#include "xenia/base/ring_buffer.h"
#include "xenia/profiling.h"

#include <cstring>

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavutil/channel_layout.h"
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

  static bool avcodec_initialized = false;
  if (!avcodec_initialized) {
    avcodec_register_all();
    avcodec_initialized = true;
  }

  // Allocate important stuff
  codec_ = avcodec_find_decoder(AV_CODEC_ID_WMAPRO);
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

  // Extra data passed to the decoder
  std::memset(&extra_data_, 0, sizeof(extra_data_));
  extra_data_.bits_per_sample = 16;
  extra_data_.channel_mask = AV_CH_FRONT_RIGHT;
  extra_data_.decode_flags = 0x10D6;

  context_->extradata_size = sizeof(extra_data_);
  context_->extradata = (uint8_t*)&extra_data_;

  // Current frame stuff whatever
  // samples per frame * 2 max channels * output bytes
  current_frame_ = new uint8_t[kSamplesPerFrame * 2 * 2];
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
  DecodePackets(data);
  data.Store(context_ptr);
}

void XmaContext::Enable() {
  std::lock_guard<xe::mutex> lock(lock_);

  auto context_ptr = memory()->TranslateVirtual(guest_ptr());
  XMA_CONTEXT_DATA data(context_ptr);

  XELOGAPU(
      "XmaContext: kicking context %d (%d/%d bytes)", id(),
      (data.input_buffer_read_offset & ~0x7FF) / 8,
      (data.input_buffer_0_packet_count + data.input_buffer_1_packet_count) *
          kBytesPerPacket);

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
  // Lock it in case the decoder thread is working on it now
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

void XmaContext::DecodePackets(XMA_CONTEXT_DATA& data) {
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
  if (!data.input_buffer_0_valid && !data.input_buffer_1_valid) {
    return;
  }

  // Check the output buffer - we cannot decode anything else if it's
  // unavailable.
  if (!data.output_buffer_valid) {
    return;
  }

  // Output buffers are in raw PCM samples, 256 bytes per block.
  // Output buffer is a ring buffer. We need to write from the write offset
  // to the read offset.
  uint8_t* output_buffer = memory()->TranslatePhysical(data.output_buffer_ptr);
  uint32_t output_capacity = data.output_buffer_block_count * kBytesPerSubframe;
  uint32_t output_read_offset =
      data.output_buffer_read_offset * kBytesPerSubframe;
  uint32_t output_write_offset =
      data.output_buffer_write_offset * kBytesPerSubframe;

  RingBuffer output_rb(output_buffer, output_capacity);
  output_rb.set_read_offset(output_read_offset);
  output_rb.set_write_offset(output_write_offset);

  size_t output_remaining_bytes = output_rb.write_count();

  // Decode until we can't write any more data.
  while (output_remaining_bytes > 0) {
    // This'll copy audio samples into the output buffer.
    // The samples need to be 2 bytes long!
    // Copies one frame at a time, so keep calling this until size == 0
    int written_bytes = 0;
    int decode_attempts_remaining = 3;

    uint8_t work_buffer[kOutputMaxSizeBytes];
    while (decode_attempts_remaining) {
      size_t read_bytes = 0;
      written_bytes =
          DecodePacket(work_buffer, 0, output_remaining_bytes, &read_bytes);
      if (written_bytes >= 0) {
        // assert_true((written_bytes % 256) == 0);
        auto written_bytes_rb = output_rb.Write(work_buffer, written_bytes);
        assert_true(written_bytes == written_bytes_rb);

        // Ok.
        break;
      } else if (read_bytes % 2048 == 0) {
        // Sometimes the decoder will fail on a packet. I think it's
        // looking for cross-packet frames and failing. If you run it again
        // on the same packet it'll work though.
        --decode_attempts_remaining;
      } else {
        // Failed in the middle of a packet, do not retry!
        decode_attempts_remaining = 0;
        break;
      }
    }

    if (!decode_attempts_remaining) {
      XELOGAPU("XmaContext: libav failed to decode packet (returned %.8X)",
               -written_bytes);

      // Failed out.
      if (data.input_buffer_0_valid || data.input_buffer_1_valid) {
        // There's new data available - maybe we'll be ok if we decode it?
        written_bytes = 0;
        DiscardPacket();
      } else {
        // No data and hosed - bail.
        break;
      }
    }

    data.output_buffer_write_offset = output_rb.write_offset() / 256;
    output_remaining_bytes -= written_bytes;

    // If we need more data and the input buffers have it, grab it.
    if (written_bytes) {
      // Haven't finished with current packet.
      continue;
    } else if (data.input_buffer_0_valid || data.input_buffer_1_valid) {
      // Done with previous packet, so grab a new one.
      int ret = StartPacket(data);
      if (ret <= 0) {
        // No more data (but may have prepared a packet)
        data.input_buffer_0_valid = 0;
        data.input_buffer_1_valid = 0;
      }
    } else {
      // Decoder is out of data and there's no more to give.
      break;
    }
  }

  // The game will kick us again with a new output buffer later.
  data.output_buffer_valid = 0;
}

int XmaContext::StartPacket(XMA_CONTEXT_DATA& data) {
  // Translate pointers for future use.
  uint8_t* in0 = data.input_buffer_0_valid
                     ? memory()->TranslatePhysical(data.input_buffer_0_ptr)
                     : nullptr;
  uint8_t* in1 = data.input_buffer_1_valid
                     ? memory()->TranslatePhysical(data.input_buffer_1_ptr)
                     : nullptr;

  int sample_rate = GetSampleRate(data.sample_rate);
  int channels = data.is_stereo ? 2 : 1;

  // See if we've finished with the input.
  // Block count is in packets, so expand by packet size.
  uint32_t input_size_0_bytes =
      data.input_buffer_0_valid ? (data.input_buffer_0_packet_count) * 2048 : 0;
  uint32_t input_size_1_bytes =
      data.input_buffer_1_valid ? (data.input_buffer_1_packet_count) * 2048 : 0;

  // Total input size
  uint32_t input_size_bytes = input_size_0_bytes + input_size_1_bytes;

  // Input read offset is in bits. Typically starts at 32 (4 bytes).
  // "Sequence" offset - used internally for WMA Pro decoder.
  // Just the read offset.
  uint32_t seq_offset_bytes = (data.input_buffer_read_offset & ~0x7FF) / 8;
  uint32_t input_remaining_bytes = input_size_bytes - seq_offset_bytes;

  if (seq_offset_bytes < input_size_bytes) {
    // Setup input offset and input buffer.
    uint32_t input_offset_bytes = seq_offset_bytes;
    auto input_buffer = in0;

    if (seq_offset_bytes >= input_size_0_bytes && input_size_1_bytes) {
      // Size overlap, select input buffer 1.
      // TODO: This needs testing.
      input_offset_bytes -= input_size_0_bytes;
      input_buffer = in1;
    }

    // Still have data to read.
    auto packet = input_buffer + input_offset_bytes;
    assert_true(input_offset_bytes % 2048 == 0);
    PreparePacket(packet, seq_offset_bytes, kBytesPerPacket, sample_rate,
                  channels);
    data.input_buffer_read_offset += kBytesPerPacket * 8;

    input_remaining_bytes -= kBytesPerPacket;
    if (input_remaining_bytes <= 0) {
      // Used the last of the data but prepared a packet
      return 0;
    }
  } else {
    // No more data available and no packet prepared.
    return -1;
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
    // Haven't finished parsing another packet
    return 1;
  }

  std::memcpy(packet_data_, input, size);

  // Modify the packet header so it's WMAPro compatible
  *((int*)packet_data_) = (((seq_offset & 0x7800) | 0x400) >> 7) |
                          (*((int*)packet_data_) & 0xFFFEFF08);

  packet_->data = packet_data_;
  packet_->size = kBytesPerPacket;

  // Re-initialize the context with new sample rate and channels
  if (context_->sample_rate != sample_rate || context_->channels != channels) {
    // We have to reopen the codec so it'll realloc whatever data it needs.
    // TODO: Find a better way.
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

    // Decode the current frame
    int len =
        avcodec_decode_audio4(context_, decoded_frame_, &got_frame, packet_);
    if (len < 0) {
      // Error in codec (bad sample rate or something)
      return len;
    }

    if (read_bytes) {
      *read_bytes += len;
    }

    // Offset by decoded length
    packet_->size -= len;
    packet_->data += len;
    packet_->dts = packet_->pts = AV_NOPTS_VALUE;

    // Successfully decoded a frame
    if (got_frame) {
      // Validity checks.
      if (decoded_frame_->nb_samples > kSamplesPerFrame) {
        return -2;
      } else if (context_->sample_fmt != AV_SAMPLE_FMT_FLTP) {
        return -3;
      }

      // Check the returned buffer size
      if (av_samples_get_buffer_size(NULL, context_->channels,
                                     decoded_frame_->nb_samples,
                                     context_->sample_fmt, 1) !=
          context_->channels * decoded_frame_->nb_samples * sizeof(float)) {
        return -4;
      }

      // Loop through every sample, convert and drop it into the output array.
      // If more than one channel, the game wants the samples from each channel
      // interleaved next to eachother
      uint32_t o = 0;
      for (int i = 0; i < decoded_frame_->nb_samples; i++) {
        for (int j = 0; j < context_->channels; j++) {
          // Select the appropriate array based on the current channel.
          float* sample_array = (float*)decoded_frame_->data[j];

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

      // Total size of the frame's samples
      // Magic number 2 is sizeof an output sample
      frame_samples_size_ = context_->channels * decoded_frame_->nb_samples * 2;

      to_copy = std::min(output_size, (size_t)(frame_samples_size_));
      std::memcpy(output + output_offset, current_frame_, to_copy);

      current_frame_pos_ += to_copy;
      output_size -= to_copy;
      output_offset += to_copy;
    }
  }

  // Return number of bytes written
  return (int)(output_offset - original_offset);
}

}  // namespace xe
}  // namespace apu
