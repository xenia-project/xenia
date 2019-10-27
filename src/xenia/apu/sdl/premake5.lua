project_root = "../../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-apu-sdl")
  uuid("153b4e8b-813a-40e6-9366-4b51abc73c45")
  kind("StaticLib")
  language("C++")
  links({
    "xenia-base",
    "xenia-apu",
    "SDL2",
  })
  defines({
  })
  local_platform_files()

  filter("platforms:Windows")
    -- On linux we build against the system version (libsdl2-dev)
    includedirs({
      project_root.."/third_party/SDL2-devel-VC/include/",
    })
    libdirs({
      project_root.."/third_party/SDL2-devel-VC/lib/x64/",
    })
    -- Copy the dll to the output folder
    postbuildcommands({
      "{COPY} %{prj.basedir}/"..project_root.."/third_party/SDL2-devel-VC/lib/x64/SDL2.dll %{cfg.targetdir}",
    })
