/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_APPS_XMP_APP_H_
#define XENIA_KERNEL_XAM_APPS_XMP_APP_H_

#include <memory>
#include <mutex>
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

struct XMP_SONGDESCRIPTOR {
  xe::be<uint32_t> file_path_ptr;
  xe::be<uint32_t> title_ptr;
  xe::be<uint32_t> artist_ptr;
  xe::be<uint32_t> album_ptr;
  xe::be<uint32_t> album_artist_ptr;
  xe::be<uint32_t> genre_ptr;
  xe::be<uint32_t> track_number;
  xe::be<uint32_t> duration;
  xe::be<uint32_t> song_format;
};
static_assert_size(XMP_SONGDESCRIPTOR, 36);

constexpr uint32_t kMaxXmpMetadataStringLength = 40;

struct XMP_SONGINFO {
  X_HANDLE handle;

  uint8_t unknown[0x23C];
  xe::be<char16_t> title[kMaxXmpMetadataStringLength];
  xe::be<char16_t> artist[kMaxXmpMetadataStringLength];
  xe::be<char16_t> album[kMaxXmpMetadataStringLength];
  xe::be<char16_t> album_artist[kMaxXmpMetadataStringLength];
  xe::be<char16_t> genre[kMaxXmpMetadataStringLength];
  xe::be<uint32_t> track_number;
  xe::be<uint32_t> duration;
  xe::be<uint32_t> song_format;
};
static_assert_size(XMP_SONGINFO, 988);

class XmpApp : public App {
 public:
  enum class State : uint32_t {
    kIdle = 0,
    kPlaying = 1,
    kPaused = 2,
  };
  enum class PlaybackClient : uint32_t {
    kSystem = 0,
    kTitle = 1,
  };
  enum class PlaybackMode : uint32_t {
    kInOrder = 0,
    kShuffle = 1,
  };
  enum class RepeatMode : uint32_t {
    kPlaylist = 0,
    kNoRepeat = 1,
  };
  enum class PlaybackFlags : uint32_t {
    kDefault = 0,
    kAutoPause = 1,
  };
  struct Song {
    enum class Format : uint32_t {
      kWma = 0,
      kMp3 = 1,
    };

    uint32_t handle;
    std::u16string file_path;
    std::u16string name;
    std::u16string artist;
    std::u16string album;
    std::u16string album_artist;
    std::u16string genre;
    uint32_t track_number;
    uint32_t duration_ms;
    Format format;
  };
  struct Playlist {
    uint32_t handle;
    std::u16string name;
    uint32_t flags;
    std::vector<std::unique_ptr<Song>> songs;
  };

  explicit XmpApp(KernelState* kernel_state);

  X_HRESULT XMPGetStatus(uint32_t status_ptr);

  X_HRESULT XMPCreateTitlePlaylist(uint32_t songs_ptr, uint32_t song_count,
                                   uint32_t playlist_name_ptr,
                                   const std::u16string& playlist_name,
                                   uint32_t flags, uint32_t out_song_handles,
                                   uint32_t out_playlist_handle);
  X_HRESULT XMPDeleteTitlePlaylist(uint32_t playlist_handle);
  X_HRESULT XMPPlayTitlePlaylist(uint32_t playlist_handle,
                                 uint32_t song_handle);
  X_HRESULT XMPContinue();
  X_HRESULT XMPStop(uint32_t unk);
  X_HRESULT XMPPause();
  X_HRESULT XMPNext();
  X_HRESULT XMPPrevious();

  X_HRESULT DispatchMessageSync(uint32_t message, uint32_t buffer_ptr,
                                uint32_t buffer_length) override;

 private:
  xe::global_critical_region global_critical_region_;

  // TODO: Remove it and replace with guest handles!
  uint32_t next_playlist_handle_ = 0;
  uint32_t next_song_handle_ = 0;
};

}  // namespace apps
}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_APPS_XMP_APP_H_
