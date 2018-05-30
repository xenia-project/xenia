/**
******************************************************************************
* Xenia : Xbox 360 Emulator Research Project                                 *
******************************************************************************
* Copyright 2015 Ben Vanik. All rights reserved.                             *
* Released under the BSD license - see LICENSE in the root for more details. *
******************************************************************************
*/

#include "discord_presence.h"
#include "third_party/discord-rpc/include/discord_rpc.h"

namespace xe {
namespace discord {

void HandleDiscordReady(const DiscordUser* request) {}
void HandleDiscordError(int errorCode, const char* message) {}
void HandleDiscordJoinGame(const char* joinSecret) {}
void HandleDiscordJoinRequest(const DiscordUser* request) {}
void HandleDiscordSpectateGame(const char* spectateSecret) {}

void DiscordPresence::InitializeDiscord() {
  DiscordEventHandlers handlers;
  memset(&handlers, 0, sizeof(handlers));
  handlers.ready = &HandleDiscordReady;
  handlers.errored = &HandleDiscordError;
  handlers.joinGame = &HandleDiscordJoinGame;
  handlers.joinRequest = &HandleDiscordJoinRequest;
  handlers.spectateGame = &HandleDiscordSpectateGame;

  Discord_Initialize("441627211611766784", &handlers, 0, "");
}

void DiscordPresence::NotPlaying() {
  DiscordRichPresence discordPresence;
  memset(&discordPresence, 0, sizeof(discordPresence));
  discordPresence.state = "Idle";
  discordPresence.details = "Standby";
  discordPresence.largeImageKey = "default";
  discordPresence.instance = 1;

  Discord_UpdatePresence(&discordPresence);
}

void DiscordPresence::PlayingTitle(std::wstring game_title) {
  DiscordRichPresence discordPresence;
  memset(&discordPresence, 0, sizeof(discordPresence));
  discordPresence.state = "In Game";
  char mb_game_title[128];  // buffer to hold game title (max 128 bytes)
  std::wcstombs(mb_game_title, game_title.c_str(),
                128);  // convert game_title from wstring to wchar* to char*
  discordPresence.details = mb_game_title;
  discordPresence.smallImageKey = "default";
  discordPresence.largeImageKey = "defaultgame";
  discordPresence.instance = 1;

  Discord_UpdatePresence(&discordPresence);
}

void DiscordPresence::ShutdownDiscord() { Discord_Shutdown(); }

}  // namespace discord
}  // namespace xe
