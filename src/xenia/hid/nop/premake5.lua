project_root = "../../../.."
include(project_root.."/build_tools")

group("src")
project("xenia-hid-nop")
  uuid("887b6f26-b0c1-43c1-a013-a37e7b9634fd")
  kind("StaticLib")
  language("C++")
  links({
    "xenia-base",
    "xenia-hid",
  })
  defines({
  })
  includedirs({
    project_root.."/build_tools/third_party/gflags/src",
  })
  local_platform_files()
