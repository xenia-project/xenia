project_root = "../../.."
include(project_root.."/build_tools")

group("src")
project("xenia-debug")
  uuid("1c33f25a-4bc0-4959-8092-4223ce749d9b")
  kind("StaticLib")
  language("C++")
  links({
    "xenia-base",
    "xenia-cpu",
  })
  defines({
  })
  includedirs({
    project_root.."/build_tools/third_party/gflags/src",
  })
  local_platform_files()
  recursive_platform_files("proto")
