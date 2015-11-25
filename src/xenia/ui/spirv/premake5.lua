project_root = "../../../.."
include(project_root.."/build_tools")

group("src")
project("xenia-ui-spirv")
  uuid("2323a069-5b29-44a3-b524-f35451a81978")
  kind("StaticLib")
  language("C++")
  links({
    "spirv-tools",
    "xenia-base",
  })
  defines({
  })
  includedirs({
    project_root.."/build_tools/third_party/gflags/src",
    project_root.."/third_party/spirv-tools/external/include",
  })
  local_platform_files()
