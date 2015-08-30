project_root = "../../.."
include(project_root.."/build_tools")

group("src")
project("xenia-gpu")
  uuid("0e8d3370-e4b1-4b05-a2e8-39ebbcdf9b17")
  kind("StaticLib")
  language("C++")
  links({
    "elemental-forms",
    "xenia-base",
    "xenia-ui",
    "xxhash",
  })
  defines({
  })
  includedirs({
    project_root.."/third_party/elemental-forms/src",
    project_root.."/build_tools/third_party/gflags/src",
  })
  local_platform_files()
