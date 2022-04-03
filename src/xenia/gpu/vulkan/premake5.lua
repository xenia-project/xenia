project_root = "../../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-gpu-vulkan")
  uuid("717590b4-f579-4162-8f23-0624e87d6cca")
  kind("StaticLib")
  language("C++")
  links({
    "fmt",
    "glslang-spirv",
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
  files({
    "../shaders/bytecode/vulkan_spirv/*.h",
  })
