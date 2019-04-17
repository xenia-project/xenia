project_root = "../../../.."
include(project_root.."/tools/build")

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
  local_platform_files()
