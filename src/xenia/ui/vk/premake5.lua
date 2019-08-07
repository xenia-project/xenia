project_root = "../../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-ui-vk")
  uuid("758e31de-c91b-44ce-acef-27752939d37f")
  kind("StaticLib")
  language("C++")
  links({
    "volk",
    "xenia-base",
    "xenia-ui",
  })
  local_platform_files()
  files({
    "shaders/bin/*.h",
  })
