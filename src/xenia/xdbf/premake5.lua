project_root = "../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-xdbf")
  uuid("a95b5fce-1083-4bff-a022-ffdd0bab3db0")
  kind("StaticLib")
  language("C++")
  links({
    "xenia-base",
  })
  defines({
  })
  includedirs({
    project_root.."third_party/gflags/src",
  })
  recursive_platform_files()
