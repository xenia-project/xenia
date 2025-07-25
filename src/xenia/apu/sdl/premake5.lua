project_root = "../../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-apu-sdl")
  uuid("153b4e8b-813a-40e6-9366-4b51abc73c45")
  kind("StaticLib")
  language("C++")
  links({
    "xenia-apu",
    "xenia-base",
    "xenia-helper-sdl",
    "SDL2",
  })
  local_platform_files()
  sdl2_include()
