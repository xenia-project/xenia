project_root = "../.."
include(project_root.."/build_tools")

group("src")
project("xenia-core")
  uuid("970f7892-f19a-4bf5-8795-478c51757bec")
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
  files({"*.h", "*.cc"})
