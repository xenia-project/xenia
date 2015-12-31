project_root = "../../.."
include(project_root.."/tools/build")

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
    project_root.."/third_party/gflags/src",
  })
  local_platform_files()
