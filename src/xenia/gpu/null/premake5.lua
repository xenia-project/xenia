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
  includedirs({
    project_root.."/third_party/Vulkan-Headers/include",
  })
  local_platform_files()
