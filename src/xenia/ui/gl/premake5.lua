project_root = "../../../.."
include(project_root.."/build_tools")

group("src")
project("xenia-ui-gl")
  uuid("623300e3-0085-4ccc-af46-d60e88cb43aa")
  kind("StaticLib")
  language("C++")
  links({
    "elemental-forms",
    "glew",
    "xenia-base",
    "xenia-ui",
  })
  defines({
    "GLEW_STATIC=1",
    "GLEW_MX=1",
  })
  includedirs({
    project_root.."/third_party/elemental-forms/src",
    project_root.."/build_tools/third_party/gflags/src",
  })
  local_platform_files()
