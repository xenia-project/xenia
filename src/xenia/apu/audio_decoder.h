/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APU_AUDIO_DECODER_H_
#define XENIA_APU_AUDIO_DECODER_H_

#include "xenia/apu/audio_system.h"

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

class AudioDecoder {
  public:
    AudioDecoder();
    ~AudioDecoder();

    int Initialize(int bits);
    void Cleanup();

    int PreparePacket(uint8_t* input, size_t size, int sample_rate, int channels);
    void DiscardPacket();

    int DecodePacket(uint8_t* output, size_t offset, size_t size);

  private:
    AVCodec* codec_;
    AVCodecContext* context_;
    AVFrame* decoded_frame_;
    AVPacket* packet_;

    uint8_t bits_per_frame_;
    uint32_t bits_;
    size_t current_frame_pos_;
    uint8_t* current_frame_;
    uint32_t frame_samples_size_;
    int offset_;

    uint8_t packet_data_[XMAContextData::kBytesPerBlock];
};

} // namespace apu
} // namespace xe


#endif  // XENIA_APU_AUDIO_DECODER_H_