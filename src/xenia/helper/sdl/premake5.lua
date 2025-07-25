project_root = "../../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-helper-sdl")
  uuid("84b00ad3-fba3-4561-96c9-1f9993b14c9c")
  kind("StaticLib")
  language("C++")
  links({
    "SDL2",
  })
  local_platform_files()
  sdl2_include()
