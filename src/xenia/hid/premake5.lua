project_root = "../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-hid")
  uuid("88a4ef38-c550-430f-8c22-8ded4e4ef601")
  kind("StaticLib")
  language("C++")
  links({
    "xenia-base",
  })
  defines({
  })
  local_platform_files()
  removefiles({"*_demo.cc"})
