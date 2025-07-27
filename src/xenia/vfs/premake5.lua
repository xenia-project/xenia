project_root = "../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-vfs")
  uuid("395c8abd-4dc9-46ed-af7a-c2b9b68a3a98")
  kind("StaticLib")
  language("C++")
  links({
    "xenia-base",
    "zstd",
    "zarchive"
  })

  recursive_platform_files()
  removefiles({"vfs_dump.cc"})

if enableMiscSubprojects then
  project("xenia-vfs-dump")
    uuid("2EF270C7-41A8-4D0E-ACC5-59693A9CCE32")
    kind("ConsoleApp")
    language("C++")
    links({
      "fmt",
      "xenia-base",
      "xenia-vfs",
    })

    files({
      "vfs_dump.cc",
      project_root.."/src/xenia/base/console_app_main_"..platform_suffix..".cc",
    })
    resincludedirs({
      project_root,
    })
end

if enableTests then
  include("testing")
end
