project_root = "../../.."
include(project_root.."/build_tools")

group("src")
project("xenia-ui")
  uuid("d0407c25-b0ea-40dc-846c-82c46fbd9fa2")
  kind("StaticLib")
  language("C++")
  links({
    "elemental-forms",
    "xenia-base",
  })
  defines({
  })
  includedirs({
    project_root.."/third_party/elemental-forms/src",
    project_root.."/build_tools/third_party/gflags/src",
  })
  local_platform_files()
