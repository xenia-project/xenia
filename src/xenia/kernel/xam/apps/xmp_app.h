/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_APPS_XMP_APP_H_
#define XENIA_KERNEL_XAM_APPS_XMP_APP_H_

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "xenia/base/mutex.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xam/app_manager.h"

namespace xe {
namespace kernel {
namespace xam {
namespace apps {

// Only source of docs for a lot of these functions:
// https://github.com/oukiar/freestyledash/blob/master/Freestyle/Scenes/Media/Music/ScnMusic.cpp

class XmpApp : public App {
 public:
  enum class State : uint32_t {
    kIdle = 0,
    kPlaying = 1,
    kPaused = 2,
  };
  enum class PlaybackMode : uint32_t {
    // kInOrder = ?,
    kUnknown = 0,
  };
  enum class RepeatMode : uint32_t {
    // kNoRepeat = ?,
    kUnknown = 0,
  };
  struct Song {
    enum class Format : uint32_t {
      kWma = 0,
      kMp3 = 1,
    };

    uint32_t handle;
    std::wstring file_path;
    std::wstring name;
    std::wstring artist;
    std::wstring album;
    std::wstring album_artist;
    std::wstring genre;
    uint32_t track_number;
    uint32_t duration_ms;
    Format format;
  };
  struct Playlist {
    uint32_t handle;
    std::wstring name;
    uint32_t flags;
    std::vector<std::unique_ptr<Song>> songs;
  };

  explicit XmpApp(KernelState* kernel_state);

  X_RESULT XMPGetStatus(uint32_t status_ptr);

  X_RESULT XMPCreateTitlePlaylist(uint32_t songs_ptr, uint32_t song_count,
                                  uint32_t playlist_name_ptr,
                                  std::wstring playlist_name, uint32_t flags,
                                  uint32_t out_song_handles,
                                  uint32_t out_playlist_handle);
  X_RESULT XMPDeleteTitlePlaylist(uint32_t playlist_handle);
  X_RESULT XMPPlayTitlePlaylist(uint32_t playlist_handle, uint32_t song_handle);
  X_RESULT XMPContinue();
  X_RESULT XMPStop(uint32_t unk);
  X_RESULT XMPPause();
  X_RESULT XMPNext();
  X_RESULT XMPPrevious();

  X_RESULT DispatchMessageSync(uint32_t message, uint32_t buffer_ptr,
                               uint32_t buffer_length) override;

 private:
  static const uint32_t kMsgStateChanged = 0x0A000001;
  static const uint32_t kMsgPlaybackBehaviorChanged = 0x0A000002;
  static const uint32_t kMsgDisableChanged = 0x0A000003;

  void OnStateChanged();

  State state_;
  uint32_t disabled_;
  PlaybackMode playback_mode_;
  RepeatMode repeat_mode_;
  uint32_t unknown_flags_;
  float volume_;
  Playlist* active_playlist_;
  int active_song_index_;

  xe::global_critical_region global_critical_region_;
  std::unordered_map<uint32_t, Playlist*> playlists_;
  uint32_t next_playlist_handle_;
  uint32_t next_song_handle_;
};

}  // namespace apps
}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_APPS_XMP_APP_H_
