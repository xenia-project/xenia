project_root = "../../.."
include(project_root.."/build_tools")

group("src")
project("xenia-kernel")
  uuid("ae185c4a-1c4f-4503-9892-328e549e871a")
  kind("StaticLib")
  language("C++")
  links({
    "elemental-forms",
    "xenia-apu",
    "xenia-base",
    "xenia-cpu",
    "xenia-gpu",
    "xenia-hid",
    "xenia-ui",
    "xenia-vfs",
  })
  defines({
  })
  includedirs({
    project_root.."/third_party/elemental-forms/src",
    project_root.."/build_tools/third_party/gflags/src",
  })
  recursive_platform_files()
  files({
    "debug_visualizers.natvis",
  })
