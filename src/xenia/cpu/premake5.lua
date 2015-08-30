project_root = "../../.."
include(project_root.."/build_tools")

group("src")
project("xenia-cpu")
  uuid("0109c91e-5a04-41ab-9168-0d5187d11298")
  kind("StaticLib")
  language("C++")
  links({
    "xenia-base",
  })
  includedirs({
    project_root.."/third_party/llvm/include",
    project_root.."/build_tools/third_party/gflags/src",
  })
  local_platform_files()
  local_platform_files("backend")
  local_platform_files("compiler")
  local_platform_files("compiler/passes")
  local_platform_files("frontend")
  local_platform_files("hir")

include("testing")
include("frontend/testing")
