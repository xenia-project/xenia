project_root = "../../../.."
include(project_root.."/tools/build")
local qt = premake.extensions.qt

group("src")
project("xenia-ui-qt")
  uuid("3AB69653-3ACB-4C0B-976C-3B7F044E3E3A")
  kind("StaticLib")
  language("C++")

  -- Setup Qt libraries
  qt.enable()
  qtmodules{"core", "gui", "widgets"}
  qtprefix "Qt5"

  configuration {"Debug"}
    qtsuffix "d"
  configuration {}

  links({
    "xenia-base",
  })
  defines({
  })
  includedirs({
    project_root.."/third_party/gflags/src",
  })
  local_platform_files()
