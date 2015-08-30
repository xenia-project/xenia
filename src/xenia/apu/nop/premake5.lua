project_root = "../../../.."
include(project_root.."/build_tools")

group("src")
project("xenia-apu-nop")
  uuid("f37dbf3a-d200-4cc0-83f0-f801b1bdd862")
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
