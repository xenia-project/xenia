/**
******************************************************************************
* Xenia : Xbox 360 Emulator Research Project                                 *
******************************************************************************
* Copyright 2015 Ben Vanik. All rights reserved.                             *
* Released under the BSD license - see LICENSE in the root for more details. *
******************************************************************************
*/

#include "xenia/apu/audio_decoder.h"

#include "xenia/apu/audio_system.h"
#include "xenia/base/logging.h"

extern "C" {
#include "libavcodec/avcodec.h"
}

// Credits for most of this code goes to:
// https://github.com/koolkdev/libertyv/blob/master/libav_wrapper/xma2dec.c

namespace xe {
namespace apu {

AudioDecoder::AudioDecoder()
    : codec_(nullptr),
      context_(nullptr),
      decoded_frame_(nullptr),
      packet_(nullptr) {}

AudioDecoder::~AudioDecoder() {
  if (context_) {
    if (context_->extradata) {
      delete context_->extradata;
    }
    if (avcodec_is_open(context_)) {
      avcodec_close(context_);
    }
    av_free(context_);
  }
  if (decoded_frame_) {
    av_frame_free(&decoded_frame_);
  }
  if (current_frame_) {
    delete current_frame_;
  }
}

int AudioDecoder::Initialize() {
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
  context_->block_align = XMAContextData::kBytesPerBlock;

  // Extra data passed to the decoder
  context_->extradata_size = 18;
  context_->extradata = new uint8_t[18];

  // Current frame stuff whatever
  // samples per frame * 2 max channels * output bytes
  current_frame_ =
      new uint8_t[XMAContextData::kSamplesPerFrame * 2 * (bits / 8)];
  current_frame_pos_ = 0;
  frame_samples_size_ = 0;

  *(short *)(context_->extradata) = 0x10;         // bits per sample
  *(int *)(context_->extradata + 2) = 1;          // channel mask
  *(short *)(context_->extradata + 14) = 0x10D6;  // decode flags

  // FYI: We're purposely not opening the context here. That is done later.

  return 0;
}

int AudioDecoder::PreparePacket(uint8_t *input, size_t seq_offset, size_t size,
                                int sample_rate, int channels) {
  if (size != XMAContextData::kBytesPerBlock) {
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
  *((int *)packet_data_) = (((seq_offset & 0x7800) | 0x400) >> 7) |
                           (*((int *)packet_data_) & 0xFFFEFF08);

  packet_->data = packet_data_;
  packet_->size = XMAContextData::kBytesPerBlock;

  // Re-initialize the context with new sample rate and channels
  if (context_->sample_rate != sample_rate || context_->channels != channels) {
    context_->sample_rate = sample_rate;
    context_->channels = channels;

    // We have to reopen the codec so it'll realloc whatever data it needs.
    // TODO: Find a better way.
    avcodec_close(context_);
    if (avcodec_open2(context_, codec_, NULL) < 0) {
      XELOGE("Audio Decoder: Failed to reopen context.");
      return 1;
    }
  }

  return 0;
}

void AudioDecoder::DiscardPacket() {
  if (packet_->size > 0 || current_frame_pos_ != frame_samples_size_) {
    packet_->data = 0;
    packet_->size = 0;
    current_frame_pos_ = frame_samples_size_;
  }
}

int AudioDecoder::DecodePacket(uint8_t *output, size_t output_offset,
                               size_t output_size) {
  size_t to_copy = 0;
  size_t original_offset = output_offset;

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

    // Offset by decoded length
    packet_->size -= len;
    packet_->data += len;
    packet_->dts = packet_->pts = AV_NOPTS_VALUE;

    // Successfully decoded a frame
    if (got_frame) {
      // Validity checks.
      if (decoded_frame_->nb_samples > XMAContextData::kSamplesPerFrame) {
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

      // Output sample array
      float *sample_array = (float *)decoded_frame_->data[0];

      // Loop through every sample, convert and drop it into the output array.
      for (int i = 0; i < decoded_frame_->nb_samples; i++) {
        // Raw sample should be within [-1, 1].
        // Clamp it, just in case.
        float raw_sample = xe::saturate(sample_array[i]);

        // Convert the sample and output it in big endian.
        float scaled_sample = raw_sample * (1 << 15);
        int sample = static_cast<int>(scaled_sample);
        xe::store_and_swap<uint16_t>(&current_frame_[i * 2],
                                      sample & 0xFFFF);
      }
      current_frame_pos_ = 0;

      // Total size of the frame's samples
      // Magic number 2 is sizeof the output
      frame_samples_size_ =
          context_->channels * decoded_frame_->nb_samples * 2;

      to_copy = std::min(output_size, (size_t)(frame_samples_size_));
      std::memcpy(output + output_offset, current_frame_, to_copy);

      current_frame_pos_ += to_copy;
      output_size -= to_copy;
      output_offset += to_copy;
    }
  }

  // Return number of bytes written (typically 2048)
  return (int)(output_offset - original_offset);
}

}  // namespace xe
}  // namespace apu
