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
      volume_(1.0f),
      active_playlist_(nullptr),
      active_song_index_(0),
      next_playlist_handle_(1),
      next_song_handle_(1) {}

X_HRESULT XmpApp::XMPGetStatus(uint32_t state_ptr) {
  if (!XThread::GetCurrentThread()->main_thread()) {
    // Some stupid games will hammer this on a thread - induce a delay
    // here to keep from starving real threads.
    xe::threading::Sleep(std::chrono::milliseconds(1));
  }

  XELOGD("XMPGetStatus({:08X})", state_ptr);
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

  if (playback_client_ == PlaybackClient::kSystem) {
    XELOGW("XMPPlayTitlePlaylist: System playback is enabled!");
    return X_E_SUCCESS;
  }

  // Start playlist?
  XELOGW("Playlist playback not supported");
  active_playlist_ = playlist;
  active_song_index_ = 0;
  state_ = State::kPlaying;
  OnStateChanged();
  kernel_state_->BroadcastNotification(kMsgPlaybackBehaviorChanged, 1);
  return X_E_SUCCESS;
}

X_HRESULT XmpApp::XMPContinue() {
  XELOGD("XMPContinue()");
  if (state_ == State::kPaused) {
    state_ = State::kPlaying;
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
  OnStateChanged();
  return X_E_SUCCESS;
}

void XmpApp::OnStateChanged() {
  kernel_state_->BroadcastNotification(kMsgStateChanged,
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
      kernel_state_->BroadcastNotification(kMsgPlaybackBehaviorChanged, 0);
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
      kernel_state_->BroadcastNotification(kMsgPlaybackControllerChanged,
                                           !args->playback_client);
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
                                   4 + uint32_t(args->song_count) * 128);
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
