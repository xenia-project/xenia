project_root = "../../../../.."
include(project_root.."/build_tools")

group("src")
project("xenia-cpu-backend-x64")
  uuid("7d8d5dce-4696-4197-952a-09506f725afe")
  kind("StaticLib")
  language("C++")
  links({
    "capstone",
    "xenia-base",
    "xenia-cpu",
  })
  defines({
    "CAPSTONE_X86_ATT_DISABLE",
    "CAPSTONE_DIET_NO",
    "CAPSTONE_X86_REDUCE_NO",
    "CAPSTONE_HAS_X86",
    "CAPSTONE_USE_SYS_DYN_MEM",
    "XBYAK_NO_OP_NAMES",
  })
  includedirs({
    project_root.."/third_party/capstone/include",
    project_root.."/build_tools/third_party/gflags/src",
  })
  local_platform_files()
