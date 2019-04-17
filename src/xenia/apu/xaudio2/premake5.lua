project_root = "../../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-apu-xaudio2")
  uuid("7a54a497-24d9-4c0e-a013-8507a04231f9")
  kind("StaticLib")
  language("C++")
  links({
    "xenia-base",
    "xenia-apu",
  })
  defines({
  })
  local_platform_files()
