project_root = "../../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-hid-keyboard")
  uuid("ab5cfd8d-7877-44cd-9526-63f7c5738609")
  kind("StaticLib")
  language("C++")
  links({
    "xenia-base",
    "xenia-hid",
    "xenia-ui",
  })
  defines({
  })
  local_platform_files()
