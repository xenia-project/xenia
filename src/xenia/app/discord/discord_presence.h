/**
******************************************************************************
* Xenia : Xbox 360 Emulator Research Project                                 *
******************************************************************************
* Copyright 2015 Ben Vanik. All rights reserved.                             *
* Released under the BSD license - see LICENSE in the root for more details. *
******************************************************************************
*/

#ifndef XENIA_DISCORD_DISCORD_PRESENCE_H_
#define XENIA_DISCORD_DISCORD_PRESENCE_H_

#include <string>

namespace xe {
namespace discord {

class DiscordPresence {
 public:
  static void Initialize();
  static void NotPlaying();
  static void PlayingTitle(const std::wstring& game_title);
  static void Shutdown();
};

}  // namespace discord
}  // namespace xe

#endif  // XENIA_DISCORD_DISCORD_PRESENCE_H_
