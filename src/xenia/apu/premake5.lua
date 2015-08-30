project_root = "../../.."
include(project_root.."/build_tools")

group("src")
project("xenia-apu")
  uuid("f4df01f0-50e4-4c67-8f54-61660696cc79")
  kind("StaticLib")
  language("C++")
  links({
    "libavcodec",
    "libavutil",
    "xenia-base",
  })
  defines({
  })
  includedirs({
    project_root.."/build_tools/third_party/gflags/src",
    project_root.."/third_party/libav/",
  })
  local_platform_files()

