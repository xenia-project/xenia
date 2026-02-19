group("third_party")
project("tomlplusplus")
  uuid("1e86cc51-3f8b-476d-9249-3b200424846b")
  if os.istarget("android") then
    -- ndk-build only supports StaticLib and SharedLib.
    kind("StaticLib")
  else
    kind("Utility")
  end
  language("C++")
  files({
    "tomlplusplus/include/toml++/toml.hpp"
  })
