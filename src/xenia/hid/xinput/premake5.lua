project_root = "../../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-hid-xinput")
  uuid("3d49e251-07a7-40ae-9bc6-aae984c85568")
  kind("StaticLib")
  language("C++")
  links({
    "xenia-base",
    "xenia-hid",
  })
  local_platform_files()
