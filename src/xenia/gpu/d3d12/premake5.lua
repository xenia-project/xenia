project_root = "../../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-gpu-d3d12")
  uuid("c057eae4-e7bb-4113-9a69-1fe07b735c49")
  kind("StaticLib")
  language("C++")
  links({
    "xenia-base",
    "xenia-gpu",
    "xenia-ui",
    "xenia-ui-d3d12",
    "xxhash",
  })
  local_platform_files()
  files({
    "shaders/bin/*.h",
  })
