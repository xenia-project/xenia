project_root = "../../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-gpu-vk")
  uuid("66c9afbb-798a-405d-80a1-7bda473e700d")
  kind("StaticLib")
  language("C++")
  links({
    "xenia-base",
    "xenia-gpu",
    "xenia-ui",
    "xenia-ui-vk",
    "xxhash",
  })
  local_platform_files()
  files({
    "shaders/bin/*.h",
  })
