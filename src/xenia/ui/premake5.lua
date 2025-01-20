project_root = "../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-ui")
  uuid("d0407c25-b0ea-40dc-846c-82c46fbd9fa2")
  kind("StaticLib")
  language("C++")
  links({
    "xenia-base",
  })
  defines({
  })
  local_platform_files()
  removefiles({"*_demo.cc"})
  removefiles({"windowed_app_main_*.cc"})
--  filter({"configurations:Release", "platforms:Windows"})
--    buildoptions({
--      "/O1",
--    })
  filter("platforms:Android-*")
    -- Exports JNI functions.
    wholelib("On")

  filter("platforms:Windows")
    links({
      "dwmapi",
      "dxgi",
      "winmm",
    })

  filter("platforms:Linux")
    links({
      "xcb",
      "X11",
      "X11-xcb"
    })