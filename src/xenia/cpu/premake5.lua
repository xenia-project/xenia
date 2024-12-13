project_root = "../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-cpu")
  uuid("0109c91e-5a04-41ab-9168-0d5187d11298")
  kind("StaticLib")
  language("C++")
  links({
    "xenia-base",
    "mspack",
  })

  includedirs({
    project_root.."/third_party/llvm/include",
  })
  local_platform_files()
  local_platform_files("backend")
  local_platform_files("compiler")
  local_platform_files("compiler/passes")
  local_platform_files("hir")
  local_platform_files("ppc")

include("testing")
include("ppc/testing")
--  filter({"configurations:Release", "platforms:Windows"})
--  buildoptions({
--    "/O1",
--  })
