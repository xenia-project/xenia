project_root = "../../../.."
include(project_root.."/build_tools")

group("src")
project("xenia-hid-winkey")
  uuid("fd16e19a-6219-4ab7-b95a-7c78523c50c3")
  kind("StaticLib")
  language("C++")
  links({
    "xenia-base",
    "xenia-hid",
  })
  defines({
  })
  includedirs({
  })
  local_platform_files()
