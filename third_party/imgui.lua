group("third_party")
project("imgui")
  uuid("ed9271c4-b0a1-42ef-8403-067b11bf49d0")
  kind("StaticLib")
  language("C++")
  links({
  })
  defines({
    "_LIB",
  })
  includedirs({
    "imgui",
  })
--  filter({"configurations:Release", "platforms:Windows"})
--    buildoptions({
--      "/O1",
--    })
--  filter{}
  files({
    "imgui/imconfig.h",
    "imgui/imgui.cpp",
    "imgui/imgui.h",
    "imgui/imgui_demo.cpp",
    "imgui/imgui_draw.cpp",
    "imgui/imgui_internal.h",
    "imgui/imgui_tables.cpp",
    "imgui/imgui_widgets.cpp",
    "imgui/imstb_rectpack.h",
    "imgui/imstb_textedit.h",
    "imgui/imstb_truetype.h",
  })
