group("third_party")
project("cpptoml")
  uuid("1e86cc51-3f8b-476d-9249-3b200424846b")
  if os.istarget("android") then
    -- ndk-build only supports StaticLib and SharedLib.
    kind("StaticLib")
  else
    kind("Utility")
  end
  language("C++")
  files({
    "cpptoml/include/cpptoml.h",
  })
