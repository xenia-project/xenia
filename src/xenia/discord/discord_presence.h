/**
******************************************************************************
* Xenia : Xbox 360 Emulator Research Project                                 *
******************************************************************************
* Copyright 2015 Ben Vanik. All rights reserved.                             *
* Released under the BSD license - see LICENSE in the root for more details. *
******************************************************************************
*/

#include <comdef.h>
#include <string>

namespace xe {
namespace discord {

class DiscordPresence
{
public:
  static void InitializeDiscord();
  static void NotPlaying();
  static void PlayingTitle(std::wstring game_title);
  static void ShutdownDiscord();
};

}
}