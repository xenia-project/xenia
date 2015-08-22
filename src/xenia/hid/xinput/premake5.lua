project_root = "../../../.."
include(project_root.."/build_tools")

group("src")
project("xenia-hid-xinput")
  uuid("3d49e251-07a7-40ae-9bc6-aae984c85568")
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
