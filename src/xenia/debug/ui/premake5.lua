project_root = "../../../.."
include(project_root.."/build_tools")

group("src")
project("xenia-debug-ui")
  uuid("9193a274-f4c2-4746-bd85-93fcfc5c3e38")
  kind("StaticLib")
  language("C++")
  links({
    "elemental-forms",
    "glew",
    "imgui",
    "xenia-base",
    "xenia-cpu",
    "xenia-debug",
    "xenia-ui",
    "xenia-ui-gl",
  })
  defines({
    "GLEW_STATIC=1",
    "GLEW_MX=1",
  })
  includedirs({
    project_root.."/build_tools/third_party/gflags/src",
    project_root.."/third_party/elemental-forms/src",
  })
  local_platform_files()
