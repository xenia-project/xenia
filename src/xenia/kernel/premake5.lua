project_root = "../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-kernel")
  uuid("ae185c4a-1c4f-4503-9892-328e549e871a")
  kind("StaticLib")
  language("C++")
  links({
    "aes_128",
    "fmt",
    "zlib",
    "pugixml",
    "xenia-apu",
    "xenia-base",
    "xenia-cpu",
    "xenia-hid",
    "xenia-vfs",
  })
  defines({
  })
  recursive_platform_files()
  files({
    "debug_visualizers.natvis",
  })
