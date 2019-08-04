project_root = "../../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-gpu-null")
  uuid("42FCA0B3-4C20-4532-95E9-07D297013BE4")
  kind("StaticLib")
  language("C++")
  links({
    "xenia-base",
    "xenia-gpu",
    "xenia-ui",
    "xenia-ui-vulkan",
    "xxhash",
  })
  defines({
  })
  local_platform_files()
