/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/apps/xmp_app.h"
#include "xenia/kernel/xthread.h"

#include "xenia/base/logging.h"
#include "xenia/base/threading.h"
#include "xenia/emulator.h"
#include "xenia/xbox.h"

#include "xenia/apu/audio_driver.h"
#include "xenia/apu/audio_system.h"

extern "C" {
#if XE_COMPILER_MSVC
#pragma warning(push)
#pragma warning(disable : 4101 4244 5033)
#endif
#include "third_party/FFmpeg/libavcodec/avcodec.h"
#include "third_party/FFmpeg/libavformat/avformat.h"
#include "third_party/FFmpeg/libavutil/opt.h"
#if XE_COMPILER_MSVC
#pragma warning(pop)
#endif
}  // extern "C"

DEFINE_bool(enable_xmp, true, "Enables Music Player playback.", "APU");
DEFINE_int32(xmp_default_volume, 70,
             "Default music volume if game doesn't set it [0-100].", "APU");

namespace xe {
namespace kernel {
namespace xam {
namespace apps {

XmpApp::XmpApp(KernelState* kernel_state)
    : App(kernel_state, 0xFA),
      state_(State::kIdle),
      playback_client_(PlaybackClient::kTitle),
      playback_mode_(PlaybackMode::kUnknown),
      repeat_mode_(RepeatMode::kUnknown),
      unknown_flags_(0),
      volume_(cvars::xmp_default_volume / 100.0f),
      active_playlist_(nullptr),
      active_song_index_(0),
      next_playlist_handle_(1),
      next_song_handle_(1) {
  if (cvars::enable_xmp) {
    worker_running_ = true;
    worker_thread_ = threading::Thread::Create({}, [&] { WorkerThreadMain(); });
    worker_thread_->set_name("Music Player");
  }
}

struct VFSContext {
  xe::vfs::File* file;
  size_t byte_offset;
};

static int xenia_vfs_read(void* opaque, uint8_t* buf, int buf_size) {
  auto ctx = static_cast<VFSContext*>(opaque);

  size_t bytes_read;
  X_STATUS status =
      ctx->file->ReadSync(buf, buf_size, ctx->byte_offset, &bytes_read);

  if (XFAILED(status)) {
    return status == X_STATUS_END_OF_FILE ? AVERROR_EOF : status;
  }

  ctx->byte_offset += bytes_read;
  return static_cast<int>(bytes_read);
}

bool XmpApp::PlayFile(std::string_view filename) {
  auto playlist = active_playlist_;

  const int buffer_size = 8192;
  uint8_t* buffer = reinterpret_cast<uint8_t*>(av_malloc(buffer_size));
  VFSContext vfs_ctx = {};

  xe::vfs::FileAction file_action;
  X_STATUS status = kernel_state_->file_system()->OpenFile(
      nullptr, filename, xe::vfs::FileDisposition::kOpen,
      xe::vfs::FileAccess::kGenericRead, false, true, &vfs_ctx.file,
      &file_action);
  if (XFAILED(status)) {
    XELOGE("Opening {} failed with status {:X}", filename, status);
    return false;
  }

  AVIOContext* avio_ctx = avio_alloc_context(buffer, buffer_size, 0, &vfs_ctx,
                                             xenia_vfs_read, nullptr, nullptr);

  AVFormatContext* formatContext = avformat_alloc_context();
  formatContext->pb = avio_ctx;
  int ret;
  if ((ret = avformat_open_input(&formatContext, nullptr, nullptr, nullptr)) !=
      0) {
    XELOGE("ffmpeg: Could not open WMA file: {:x}", ret);
    av_freep(&avio_ctx->buffer);
    avio_context_free(&avio_ctx);
    return false;
  }

  if (avformat_find_stream_info(formatContext, nullptr) < 0) {
    XELOGE("ffmpeg: Could not find stream info");
    avformat_close_input(&formatContext);
    av_freep(&avio_ctx->buffer);
    avio_context_free(&avio_ctx);
    return false;
  }

  AVCodec* codec = nullptr;
  int streamIndex =
      av_find_best_stream(formatContext, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
  if (streamIndex < 0) {
    XELOGE("ffmpeg: Could not find audio stream");
    avformat_close_input(&formatContext);
    av_freep(&avio_ctx->buffer);
    avio_context_free(&avio_ctx);
    return false;
  }

  AVStream* audioStream = formatContext->streams[streamIndex];
  AVCodecContext* codecContext = avcodec_alloc_context3(codec);
  avcodec_parameters_to_context(codecContext, audioStream->codecpar);

  if (audioStream->codecpar->format != AV_SAMPLE_FMT_FLTP &&
      audioStream->codecpar->format != AV_SAMPLE_FMT_FLT) {
    XELOGE("Audio stream has unexpected sample format {:d}",
           audioStream->codecpar->format);
    avcodec_free_context(&codecContext);
    avformat_close_input(&formatContext);
    av_freep(&avio_ctx->buffer);
    avio_context_free(&avio_ctx);
    return false;
  }

  if (avcodec_open2(codecContext, codec, nullptr) < 0) {
    XELOGE("ffmpeg: Could not open codec");
    avcodec_free_context(&codecContext);
    avformat_close_input(&formatContext);
    av_freep(&avio_ctx->buffer);
    avio_context_free(&avio_ctx);
    return false;
  }

  auto driverReady = xe::threading::Semaphore::Create(64, 64);
  {
    std::unique_lock<std::mutex> guard(driver_mutex_);
    driver_ = kernel_state_->emulator()->audio_system()->CreateDriver(
        driverReady.get(), codecContext->sample_rate, codecContext->channels,
        false);
    if (!driver_->Initialize()) {
      XELOGE("Driver initialization failed!");
      driver_->Shutdown();
      driver_ = nullptr;
      avcodec_free_context(&codecContext);
      avformat_close_input(&formatContext);
      av_freep(&avio_ctx->buffer);
      avio_context_free(&avio_ctx);
      return false;
    }
  }
  if (volume_ == 0.0f) {
    // Some games set volume to 0 on startup and then never call SetVolume
    // again...
    volume_ = cvars::xmp_default_volume / 100.0f;
  }
  driver_->SetVolume(volume_);

  AVPacket* packet = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  std::vector<float> frameBuffer;

  // Read frames, decode & send to audio driver
  while (av_read_frame(formatContext, packet) >= 0) {
    if (active_playlist_ != playlist) {
      frameBuffer.clear();
      break;
    }

    if (packet->stream_index == streamIndex) {
      int ret = avcodec_send_packet(codecContext, packet);
      if (ret < 0) {
        XELOGE("Error sending packet for decoding: {:X}", ret);
        break;
      }

      while (ret >= 0) {
        ret = avcodec_receive_frame(codecContext, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) {
          XELOGW("Error during decoding: {:X}", ret);
          break;
        }

        // If the frame is planar, convert it to interleaved
        if (frame->format == AV_SAMPLE_FMT_FLTP) {
          for (int sample = 0; sample < frame->nb_samples; sample++) {
            for (int ch = 0; ch < codecContext->channels; ch++) {
              float sampleValue =
                  reinterpret_cast<float*>(frame->data[ch])[sample];
              frameBuffer.push_back(sampleValue);
            }
          }
        } else if (frame->format == AV_SAMPLE_FMT_FLT) {
          int frameSizeFloats = frame->nb_samples * codecContext->channels;
          float* frameData = reinterpret_cast<float*>(frame->data[0]);
          frameBuffer.insert(frameBuffer.end(), frameData,
                             frameData + frameSizeFloats);
        }

        while (frameBuffer.size() >= xe::apu::AudioDriver::kFrameSamplesMax) {
          xe::threading::Wait(driverReady.get(), true);

          if (active_playlist_ != playlist) {
            frameBuffer.clear();
            break;
          }

          driver_->SubmitFrame(frameBuffer.data());
          frameBuffer.erase(
              frameBuffer.begin(),
              frameBuffer.begin() + xe::apu::AudioDriver::kFrameSamplesMax);
        }
      }
    }
    av_packet_unref(packet);
  }

  if (!frameBuffer.empty()) {
    while (frameBuffer.size() < xe::apu::AudioDriver::kFrameSamplesMax) {
      frameBuffer.push_back(0.0f);
    }

    xe::threading::Wait(driverReady.get(), true);
    driver_->SubmitFrame(frameBuffer.data());
  }

  av_frame_free(&frame);
  av_packet_free(&packet);
  avcodec_free_context(&codecContext);
  avformat_close_input(&formatContext);
  av_freep(&avio_ctx->buffer);
  avio_context_free(&avio_ctx);

  {
    std::unique_lock<std::mutex> guard(driver_mutex_);
    driver_->Shutdown();
    driver_ = nullptr;
  }

  if (state_ == State::kPlaying && active_playlist_ == playlist) {
    active_playlist_ = nullptr;
    state_ = State::kIdle;
    OnStateChanged();
  }

  return true;
}

void XmpApp::WorkerThreadMain() {
  while (worker_running_) {
    if (state_ != State::kPlaying) {
      resume_fence_.Wait();
    }

    auto playlist = active_playlist_;
    if (!playlist) {
      continue;
    }

    auto utf8_path = xe::path_to_utf8(playlist->songs[0].get()->file_path);
    XELOGI("Playing file {}", utf8_path);

    if (!PlayFile(utf8_path)) {
      XELOGE("Playback failed");
      xe::threading::Sleep(std::chrono::minutes(1));
    }
  }
}

X_HRESULT XmpApp::XMPGetStatus(uint32_t state_ptr) {
  if (!XThread::GetCurrentThread()->main_thread()) {
    // Some stupid games will hammer this on a thread - induce a delay
    // here to keep from starving real threads.
    xe::threading::Sleep(std::chrono::milliseconds(1));
  }

  XELOGD("XMPGetStatus({:08X}) -> {:d}", state_ptr, (uint32_t)state_);
  xe::store_and_swap<uint32_t>(memory_->TranslateVirtual(state_ptr),
                               static_cast<uint32_t>(state_));
  return X_E_SUCCESS;
}

X_HRESULT XmpApp::XMPCreateTitlePlaylist(
    uint32_t songs_ptr, uint32_t song_count, uint32_t playlist_name_ptr,
    const std::u16string& playlist_name, uint32_t flags,
    uint32_t out_song_handles, uint32_t out_playlist_handle) {
  XELOGD(
      "XMPCreateTitlePlaylist({:08X}, {:08X}, {:08X}({}), {:08X}, {:08X}, "
      "{:08X})",
      songs_ptr, song_count, playlist_name_ptr, xe::to_utf8(playlist_name),
      flags, out_song_handles, out_playlist_handle);
  auto playlist = std::make_unique<Playlist>();
  playlist->handle = ++next_playlist_handle_;
  playlist->name = playlist_name;
  playlist->flags = flags;
  if (songs_ptr) {
    for (uint32_t i = 0; i < song_count; ++i) {
      auto song = std::make_unique<Song>();
      song->handle = ++next_song_handle_;
      uint8_t* song_base = memory_->TranslateVirtual(songs_ptr + (i * 36));
      song->file_path =
          xe::load_and_swap<std::u16string>(memory_->TranslateVirtual(
              xe::load_and_swap<uint32_t>(song_base + 0)));
      song->name = xe::load_and_swap<std::u16string>(memory_->TranslateVirtual(
          xe::load_and_swap<uint32_t>(song_base + 4)));
      song->artist =
          xe::load_and_swap<std::u16string>(memory_->TranslateVirtual(
              xe::load_and_swap<uint32_t>(song_base + 8)));
      song->album = xe::load_and_swap<std::u16string>(memory_->TranslateVirtual(
          xe::load_and_swap<uint32_t>(song_base + 12)));
      song->album_artist =
          xe::load_and_swap<std::u16string>(memory_->TranslateVirtual(
              xe::load_and_swap<uint32_t>(song_base + 16)));
      song->genre = xe::load_and_swap<std::u16string>(memory_->TranslateVirtual(
          xe::load_and_swap<uint32_t>(song_base + 20)));
      song->track_number = xe::load_and_swap<uint32_t>(song_base + 24);
      song->duration_ms = xe::load_and_swap<uint32_t>(song_base + 28);
      song->format = static_cast<Song::Format>(
          xe::load_and_swap<uint32_t>(song_base + 32));
      if (out_song_handles) {
        xe::store_and_swap<uint32_t>(
            memory_->TranslateVirtual(out_song_handles + (i * 4)),
            song->handle);
      }
      playlist->songs.emplace_back(std::move(song));
    }
  }
  if (out_playlist_handle) {
    xe::store_and_swap<uint32_t>(memory_->TranslateVirtual(out_playlist_handle),
                                 playlist->handle);
  }

  auto global_lock = global_critical_region_.Acquire();
  playlists_.insert({playlist->handle, playlist.get()});
  playlist.release();
  return X_E_SUCCESS;
}

X_HRESULT XmpApp::XMPDeleteTitlePlaylist(uint32_t playlist_handle) {
  XELOGD("XMPDeleteTitlePlaylist({:08X})", playlist_handle);
  auto global_lock = global_critical_region_.Acquire();
  auto it = playlists_.find(playlist_handle);
  if (it == playlists_.end()) {
    XELOGE("Playlist {:08X} not found", playlist_handle);
    return X_E_NOTFOUND;
  }
  auto playlist = it->second;
  if (playlist == active_playlist_) {
    XMPStop(0);
  }
  playlists_.erase(it);
  delete playlist;
  return X_E_SUCCESS;
}

X_HRESULT XmpApp::XMPPlayTitlePlaylist(uint32_t playlist_handle,
                                       uint32_t song_handle) {
  XELOGD("XMPPlayTitlePlaylist({:08X}, {:08X})", playlist_handle, song_handle);
  Playlist* playlist = nullptr;
  {
    auto global_lock = global_critical_region_.Acquire();
    auto it = playlists_.find(playlist_handle);
    if (it == playlists_.end()) {
      XELOGE("Playlist {:08X} not found", playlist_handle);
      return X_E_NOTFOUND;
    }
    playlist = it->second;
  }

  active_playlist_ = playlist;
  active_song_index_ = 0;
  state_ = State::kPlaying;
  resume_fence_.Signal();
  OnStateChanged();
  kernel_state_->BroadcastNotification(kNotificationXmpPlaybackBehaviorChanged,
                                       1);
  return X_E_SUCCESS;
}

X_HRESULT XmpApp::XMPContinue() {
  XELOGD("XMPContinue()");
  if (state_ == State::kPaused) {
    state_ = State::kPlaying;
    resume_fence_.Signal();
    {
      std::unique_lock<std::mutex> guard(driver_mutex_);
      if (driver_ != nullptr) driver_->Resume();
    }
  }
  OnStateChanged();
  return X_E_SUCCESS;
}

X_HRESULT XmpApp::XMPStop(uint32_t unk) {
  assert_zero(unk);
  XELOGD("XMPStop({:08X})", unk);
  active_playlist_ = nullptr;  // ?
  active_song_index_ = 0;
  state_ = State::kIdle;
  OnStateChanged();
  return X_E_SUCCESS;
}

X_HRESULT XmpApp::XMPPause() {
  XELOGD("XMPPause()");
  if (state_ == State::kPlaying) {
    {
      std::unique_lock<std::mutex> guard(driver_mutex_);
      if (driver_ != nullptr) driver_->Pause();
    }
    state_ = State::kPaused;
  }
  OnStateChanged();
  return X_E_SUCCESS;
}

X_HRESULT XmpApp::XMPNext() {
  XELOGD("XMPNext()");
  if (!active_playlist_) {
    return X_E_NOTFOUND;
  }
  state_ = State::kPlaying;
  active_song_index_ =
      (active_song_index_ + 1) % active_playlist_->songs.size();
  resume_fence_.Signal();
  OnStateChanged();
  return X_E_SUCCESS;
}

X_HRESULT XmpApp::XMPPrevious() {
  XELOGD("XMPPrevious()");
  if (!active_playlist_) {
    return X_E_NOTFOUND;
  }
  state_ = State::kPlaying;
  if (!active_song_index_) {
    active_song_index_ = static_cast<int>(active_playlist_->songs.size()) - 1;
  } else {
    --active_song_index_;
  }
  resume_fence_.Signal();
  OnStateChanged();
  return X_E_SUCCESS;
}

void XmpApp::OnStateChanged() {
  kernel_state_->BroadcastNotification(kNotificationXmpStateChanged,
                                       static_cast<uint32_t>(state_));
}

X_HRESULT XmpApp::DispatchMessageSync(uint32_t message, uint32_t buffer_ptr,
                                      uint32_t buffer_length) {
  // NOTE: buffer_length may be zero or valid.
  auto buffer = memory_->TranslateVirtual(buffer_ptr);
  switch (message) {
    case 0x00070002: {
      assert_true(!buffer_length || buffer_length == 12);
      uint32_t xmp_client = xe::load_and_swap<uint32_t>(buffer + 0);
      uint32_t storage_ptr = xe::load_and_swap<uint32_t>(buffer + 4);
      uint32_t song_handle = xe::load_and_swap<uint32_t>(buffer + 8);  // 0?
      uint32_t playlist_handle =
          xe::load_and_swap<uint32_t>(memory_->TranslateVirtual(storage_ptr));
      assert_true(xmp_client == 0x00000002);
      return XMPPlayTitlePlaylist(playlist_handle, song_handle);
    }
    case 0x00070003: {
      assert_true(!buffer_length || buffer_length == 4);
      uint32_t xmp_client = xe::load_and_swap<uint32_t>(buffer + 0);
      assert_true(xmp_client == 0x00000002);
      return XMPContinue();
    }
    case 0x00070004: {
      assert_true(!buffer_length || buffer_length == 8);
      uint32_t xmp_client = xe::load_and_swap<uint32_t>(buffer + 0);
      uint32_t unk = xe::load_and_swap<uint32_t>(buffer + 4);
      assert_true(xmp_client == 0x00000002);
      return XMPStop(unk);
    }
    case 0x00070005: {
      assert_true(!buffer_length || buffer_length == 4);
      uint32_t xmp_client = xe::load_and_swap<uint32_t>(buffer + 0);
      assert_true(xmp_client == 0x00000002);
      return XMPPause();
    }
    case 0x00070006: {
      assert_true(!buffer_length || buffer_length == 4);
      uint32_t xmp_client = xe::load_and_swap<uint32_t>(buffer + 0);
      assert_true(xmp_client == 0x00000002);
      return XMPNext();
    }
    case 0x00070007: {
      assert_true(!buffer_length || buffer_length == 4);
      uint32_t xmp_client = xe::load_and_swap<uint32_t>(buffer + 0);
      assert_true(xmp_client == 0x00000002);
      return XMPPrevious();
    }
    case 0x00070008: {
      assert_true(!buffer_length || buffer_length == 16);
      struct {
        xe::be<uint32_t> xmp_client;
        xe::be<uint32_t> playback_mode;
        xe::be<uint32_t> repeat_mode;
        xe::be<uint32_t> flags;
      }* args = memory_->TranslateVirtual<decltype(args)>(buffer_ptr);
      static_assert_size(decltype(*args), 16);

      assert_true(args->xmp_client == 0x00000002 ||
                  args->xmp_client == 0x00000000);
      XELOGD("XMPSetPlaybackBehavior({:08X}, {:08X}, {:08X})",
             uint32_t(args->playback_mode), uint32_t(args->repeat_mode),
             uint32_t(args->flags));
      playback_mode_ = static_cast<PlaybackMode>(uint32_t(args->playback_mode));
      repeat_mode_ = static_cast<RepeatMode>(uint32_t(args->repeat_mode));
      unknown_flags_ = args->flags;
      kernel_state_->BroadcastNotification(
          kNotificationXmpPlaybackBehaviorChanged, 0);
      return X_E_SUCCESS;
    }
    case 0x00070009: {
      assert_true(!buffer_length || buffer_length == 8);
      uint32_t xmp_client = xe::load_and_swap<uint32_t>(buffer + 0);
      uint32_t state_ptr =
          xe::load_and_swap<uint32_t>(buffer + 4);  // out ptr to 4b - expect 0
      assert_true(xmp_client == 0x00000002);
      return XMPGetStatus(state_ptr);
    }
    case 0x0007000B: {
      assert_true(!buffer_length || buffer_length == 8);
      struct {
        xe::be<uint32_t> xmp_client;
        xe::be<uint32_t> volume_ptr;
      }* args = memory_->TranslateVirtual<decltype(args)>(buffer_ptr);
      static_assert_size(decltype(*args), 8);

      assert_true(args->xmp_client == 0x00000002);
      XELOGD("XMPGetVolume({:08X})", uint32_t(args->volume_ptr));
      xe::store_and_swap<float>(memory_->TranslateVirtual(args->volume_ptr),
                                volume_);
      return X_E_SUCCESS;
    }
    case 0x0007000C: {
      assert_true(!buffer_length || buffer_length == 8);
      struct {
        xe::be<uint32_t> xmp_client;
        xe::be<float> value;
      }* args = memory_->TranslateVirtual<decltype(args)>(buffer_ptr);
      static_assert_size(decltype(*args), 8);

      assert_true(args->xmp_client == 0x00000002);
      XELOGD("XMPSetVolume({:g})", float(args->value));
      volume_ = args->value;
      {
        std::unique_lock<std::mutex> guard(driver_mutex_);
        if (driver_ != nullptr) driver_->SetVolume(volume_);
      }
      return X_E_SUCCESS;
    }
    case 0x0007000D: {
      assert_true(!buffer_length || buffer_length == 36);
      struct {
        xe::be<uint32_t> xmp_client;
        xe::be<uint32_t> storage_ptr;
        xe::be<uint32_t> storage_size;
        xe::be<uint32_t> songs_ptr;
        xe::be<uint32_t> song_count;
        xe::be<uint32_t> playlist_name_ptr;
        xe::be<uint32_t> flags;
        xe::be<uint32_t> song_handles_ptr;
        xe::be<uint32_t> playlist_handle_ptr;
      }* args = memory_->TranslateVirtual<decltype(args)>(buffer_ptr);
      static_assert_size(decltype(*args), 36);

      xe::store_and_swap<uint32_t>(
          memory_->TranslateVirtual(args->playlist_handle_ptr),
          args->storage_ptr);
      assert_true(args->xmp_client == 0x00000002 ||
                  args->xmp_client == 0x00000000);
      std::u16string playlist_name;
      if (!args->playlist_name_ptr) {
        playlist_name = u"";
      } else {
        playlist_name = xe::load_and_swap<std::u16string>(
            memory_->TranslateVirtual(args->playlist_name_ptr));
      }
      // dummy_alloc_ptr is the result of a XamAlloc of storage_size.
      assert_true(uint32_t(args->storage_size) ==
                  4 + uint32_t(args->song_count) * 128);
      return XMPCreateTitlePlaylist(args->songs_ptr, args->song_count,
                                    args->playlist_name_ptr, playlist_name,
                                    args->flags, args->song_handles_ptr,
                                    args->storage_ptr);
    }
    case 0x0007000E: {
      assert_true(!buffer_length || buffer_length == 12);
      struct {
        xe::be<uint32_t> xmp_client;
        xe::be<uint32_t> unk_ptr;  // 0
        xe::be<uint32_t> info_ptr;
      }* args = memory_->TranslateVirtual<decltype(args)>(buffer_ptr);
      static_assert_size(decltype(*args), 12);

      auto info = memory_->TranslateVirtual(args->info_ptr);
      assert_true(args->xmp_client == 0x00000002);
      assert_zero(args->unk_ptr);
      XELOGE("XMPGetInfo?({:08X}, {:08X})", uint32_t(args->unk_ptr),
             uint32_t(args->info_ptr));
      if (!active_playlist_) {
        return X_E_FAIL;
      }
      auto& song = active_playlist_->songs[active_song_index_];
      xe::store_and_swap<uint32_t>(info + 0, song->handle);
      xe::store_and_swap<std::u16string>(info + 4 + 572 + 0, song->name);
      xe::store_and_swap<std::u16string>(info + 4 + 572 + 40, song->artist);
      xe::store_and_swap<std::u16string>(info + 4 + 572 + 80, song->album);
      xe::store_and_swap<std::u16string>(info + 4 + 572 + 120,
                                         song->album_artist);
      xe::store_and_swap<std::u16string>(info + 4 + 572 + 160, song->genre);
      xe::store_and_swap<uint32_t>(info + 4 + 572 + 200, song->track_number);
      xe::store_and_swap<uint32_t>(info + 4 + 572 + 204, song->duration_ms);
      xe::store_and_swap<uint32_t>(info + 4 + 572 + 208,
                                   static_cast<uint32_t>(song->format));
      return X_E_SUCCESS;
    }
    case 0x00070013: {
      assert_true(!buffer_length || buffer_length == 8);
      struct {
        xe::be<uint32_t> xmp_client;
        xe::be<uint32_t> storage_ptr;
      }* args = memory_->TranslateVirtual<decltype(args)>(buffer_ptr);
      static_assert_size(decltype(*args), 8);

      uint32_t playlist_handle = xe::load_and_swap<uint32_t>(
          memory_->TranslateVirtual(args->storage_ptr));
      assert_true(args->xmp_client == 0x00000002 ||
                  args->xmp_client == 0x00000000);
      return XMPDeleteTitlePlaylist(playlist_handle);
    }
    case 0x0007001A: {
      // XMPSetPlaybackController
      assert_true(!buffer_length || buffer_length == 12);
      struct {
        xe::be<uint32_t> xmp_client;
        xe::be<uint32_t> controller;
        xe::be<uint32_t> playback_client;
      }* args = memory_->TranslateVirtual<decltype(args)>(buffer_ptr);
      static_assert_size(decltype(*args), 12);

      assert_true(
          (args->xmp_client == 0x00000002 && args->controller == 0x00000000) ||
          (args->xmp_client == 0x00000000 && args->controller == 0x00000001));
      XELOGD("XMPSetPlaybackController({:08X}, {:08X})",
             uint32_t(args->controller), uint32_t(args->playback_client));

      playback_client_ = PlaybackClient(uint32_t(args->playback_client));
      kernel_state_->BroadcastNotification(
          kNotificationXmpPlaybackControllerChanged, !args->playback_client);
      return X_E_SUCCESS;
    }
    case 0x0007001B: {
      // XMPGetPlaybackController
      assert_true(!buffer_length || buffer_length == 12);
      struct {
        xe::be<uint32_t> xmp_client;
        xe::be<uint32_t> controller_ptr;
        xe::be<uint32_t> locked_ptr;
      }* args = memory_->TranslateVirtual<decltype(args)>(buffer_ptr);
      static_assert_size(decltype(*args), 12);

      assert_true(args->xmp_client == 0x00000002);
      XELOGD("XMPGetPlaybackController({:08X}, {:08X}, {:08X})",
             uint32_t(args->xmp_client), uint32_t(args->controller_ptr),
             uint32_t(args->locked_ptr));
      xe::store_and_swap<uint32_t>(
          memory_->TranslateVirtual(args->controller_ptr), 0);
      xe::store_and_swap<uint32_t>(memory_->TranslateVirtual(args->locked_ptr),
                                   0);

      if (!XThread::GetCurrentThread()->main_thread()) {
        // Atrain spawns a thread 82437FD0 to call this in a tight loop forever.
        xe::threading::Sleep(std::chrono::milliseconds(10));
      }

      return X_E_SUCCESS;
    }
    case 0x00070025: {
      // XMPCreateUserPlaylistEnumerator
      // For whatever reason buffer_length is 0 in this case.
      // Return buffer size is set to be items * 0x338 bytes.
      return X_E_SUCCESS;
    }
    case 0x00070029: {
      // XMPGetPlaybackBehavior
      assert_true(!buffer_length || buffer_length == 16);
      struct {
        xe::be<uint32_t> xmp_client;
        xe::be<uint32_t> playback_mode_ptr;
        xe::be<uint32_t> repeat_mode_ptr;
        xe::be<uint32_t> unk3_ptr;
      }* args = memory_->TranslateVirtual<decltype(args)>(buffer_ptr);
      static_assert_size(decltype(*args), 16);

      assert_true(args->xmp_client == 0x00000002 ||
                  args->xmp_client == 0x00000000);
      XELOGD("XMPGetPlaybackBehavior({:08X}, {:08X}, {:08X})",
             uint32_t(args->playback_mode_ptr), uint32_t(args->repeat_mode_ptr),
             uint32_t(args->unk3_ptr));
      if (args->playback_mode_ptr) {
        xe::store_and_swap<uint32_t>(
            memory_->TranslateVirtual(args->playback_mode_ptr),
            static_cast<uint32_t>(playback_mode_));
      }
      if (args->repeat_mode_ptr) {
        xe::store_and_swap<uint32_t>(
            memory_->TranslateVirtual(args->repeat_mode_ptr),
            static_cast<uint32_t>(repeat_mode_));
      }
      if (args->unk3_ptr) {
        xe::store_and_swap<uint32_t>(memory_->TranslateVirtual(args->unk3_ptr),
                                     unknown_flags_);
      }
      return X_E_SUCCESS;
    }
    case 0x0007002B: {
      // Called on the NXE and Kinect dashboard after clicking on the picture,
      // video, and music library
      XELOGD("XMPUnk7002B, unimplemented");
      return X_E_FAIL;
    }
    case 0x0007002E: {
      assert_true(!buffer_length || buffer_length == 12);
      // Query of size for XamAlloc - the result of the alloc is passed to
      // 0x0007000D.
      struct {
        xe::be<uint32_t> xmp_client;
        xe::be<uint32_t> song_count;
        xe::be<uint32_t> size_ptr;
      }* args = memory_->TranslateVirtual<decltype(args)>(buffer_ptr);
      static_assert_size(decltype(*args), 12);

      assert_true(args->xmp_client == 0x00000002 ||
                  args->xmp_client == 0x00000000);
      // We don't use the storage, so just fudge the number.
      xe::store_and_swap<uint32_t>(memory_->TranslateVirtual(args->size_ptr),
                                   args->song_count * 0x3E8 + 0x88);
      return X_E_SUCCESS;
    }
    case 0x0007002F: {
      // Called on the start up of all dashboard versions before kinect
      XELOGD("XMPUnk7002F, unimplemented");
      return X_E_FAIL;
    }
    case 0x0007003D: {
      // XMPCaptureOutput - not sure how this works :/
      XELOGD("XMPCaptureOutput(...)");
      assert_always("XMP output not unimplemented");
      return X_E_FAIL;
    }
    case 0x00070044: {
      // Called on the start up of all dashboard versions before kinect
      // When it returns X_E_FAIL you can access the music player up to version
      // 5787
      XELOGD("XMPUnk70044, unimplemented");
      return X_E_FAIL;
    }
    case 0x00070053: {
      // Called on the blades dashboard after clicking on the picture,
      // video, and music library
      XELOGD("XMPUnk70053, unimplemented");
      return X_E_FAIL;
    }
  }
  XELOGE(
      "Unimplemented XMP message app={:08X}, msg={:08X}, arg1={:08X}, "
      "arg2={:08X}",
      app_id(), message, buffer_ptr, buffer_length);
  return X_E_FAIL;
}

}  // namespace apps
}  // namespace xam
}  // namespace kernel
}  // namespace xe
