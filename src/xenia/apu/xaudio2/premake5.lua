project_root = "../../../.."
include(project_root.."/build_tools")

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
  includedirs({
    project_root.."/build_tools/third_party/gflags/src",
  })
  local_platform_files()
