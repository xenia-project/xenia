project_root = "../../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-app-discord")
  uuid("d14c0885-22d2-40de-ab28-7b234ef2b949")
  kind("StaticLib")
  language("C++")
  links({
    "discord-rpc"
  })
  defines({
  })
  includedirs({
    project_root.."/third_party/discord-rpc/src"
  })
  files({
    "discord_presence.cc",
    "discord_presence.h"
  })
