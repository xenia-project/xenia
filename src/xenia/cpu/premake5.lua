project_root = "../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-cpu")
  uuid("0109c91e-5a04-41ab-9168-0d5187d11298")
  kind("StaticLib")
  language("C++")
  cppdialect("C++20")
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
  
  -- Note: ARM64 backend (backend/a64) is built as a separate project (xenia-cpu-backend-a64)
  -- and linked only on ARM64 platforms. Do not include it here.

include("testing")
include("ppc/testing")
