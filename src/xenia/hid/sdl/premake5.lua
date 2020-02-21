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
