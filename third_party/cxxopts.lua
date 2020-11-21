group("third_party")
project("cxxopts")
  uuid("8b68cbe8-2da4-4f28-be14-9352eafa3168")
  if os.istarget("android") then
    -- ndk-build only supports StaticLib and SharedLib.
    kind("StaticLib")
  else
    kind("Utility")
  end
  language("C++")
  files({
    "cxxopts/include/cxxopts.hpp",
  })
  warnings("Off")
