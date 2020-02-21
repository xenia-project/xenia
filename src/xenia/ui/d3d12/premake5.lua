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
  local_platform_files()
  files({
    "shaders/bin/*.h",
  })
