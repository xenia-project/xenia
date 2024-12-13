project_root = "../../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-ui-d3d12")
  uuid("f93dc1a8-600f-43e7-b0fc-ae3eefbe836b")
  kind("StaticLib")
  language("C++")
  links({
    "xenia-base",
    "xenia-ui",
  })
--  filter({"configurations:Release", "platforms:Windows"})
--    buildoptions({
--      "/O1",
--    })
--  filter {}
  local_platform_files()
  files({
    "../shaders/bytecode/d3d12_5_1/*.h",
  })

group("demos")
project("xenia-ui-window-d3d12-demo")
  uuid("3b9686a7-0f04-4e17-8b00-aeb78ae1107c")
  single_library_windowed_app_kind()
  language("C++")
  links({
    "fmt",
    "imgui",
    "xenia-base",
    "xenia-ui",
    "xenia-ui-d3d12",
  })
  files({
    "../window_demo.cc",
    "d3d12_window_demo.cc",
    project_root.."/src/xenia/ui/windowed_app_main_"..platform_suffix..".cc",
  })
  resincludedirs({
    project_root,
  })
