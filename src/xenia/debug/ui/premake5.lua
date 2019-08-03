project_root = "../../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-debug-ui")
  uuid("9193a274-f4c2-4746-bd85-93fcfc5c3e38")
  kind("StaticLib")
  language("C++")
  links({
    "imgui",
    "xenia-base",
    "xenia-cpu",
    "xenia-ui",
    "yaml-cpp",
  })
  defines({
  })
  includedirs({
    project_root.."/third_party/yaml-cpp/include/",
  })
  local_platform_files()
