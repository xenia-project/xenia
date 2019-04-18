project_root = "../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-vfs")
  uuid("395c8abd-4dc9-46ed-af7a-c2b9b68a3a98")
  kind("StaticLib")
  language("C++")
  links({
    "xenia-base",
  })
  defines({
  })
  includedirs({
    project_root.."/third_party/gflags/src",
  })
  recursive_platform_files()
  removefiles({"vfs_dump.cc"})

project("xenia-vfs-dump")
  uuid("2EF270C7-41A8-4D0E-ACC5-59693A9CCE32")
  kind("ConsoleApp")
  language("C++")
  links({
    "gflags",
    "xenia-base",
    "xenia-vfs",
  })
  defines({})
  includedirs({
    project_root.."/third_party/gflags/src",
  })

  files({
    "vfs_dump.cc",
    project_root.."/src/xenia/base/main_"..platform_suffix..".cc",
  })
  resincludedirs({
    project_root,
  })

