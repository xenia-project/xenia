project_root = "../../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-hid-sdl")
  uuid("44f5b9a1-00f8-4825-acf1-5c93f26eba9b")
  kind("StaticLib")
  language("C++")
  links({
    "xenia-base",
    "xenia-hid",
    "xenia-ui",
    "SDL2",
  })
  local_platform_files()
  sdl2_include()
