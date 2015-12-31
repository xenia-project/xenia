project_root = "../../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-debug-ui")
  uuid("9193a274-f4c2-4746-bd85-93fcfc5c3e38")
  kind("StaticLib")
  language("C++")
  links({
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
    project_root.."/third_party/gflags/src",
  })
  local_platform_files()
