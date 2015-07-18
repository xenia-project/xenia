project_root = "../../.."
include(project_root.."/build_tools")

group("src")
project("xenia-cpu")
  uuid("0109c91e-5a04-41ab-9168-0d5187d11298")
  kind("StaticLib")
  language("C++")
  links({
    "beaengine",
    "xenia-base",
  })
  defines({
    "BEA_ENGINE_STATIC=1",
  })
  includedirs({
    project_root.."/third_party/beaengine/include",
    project_root.."/third_party/llvm/include",
  })
  local_platform_files()
  local_platform_files("backend")
  local_platform_files("compiler")
  local_platform_files("compiler/passes")
  local_platform_files("frontend")
  local_platform_files("hir")

include("testing")
include("frontend/testing")
