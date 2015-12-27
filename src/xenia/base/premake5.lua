project_root = "../../.."
include(project_root.."/build_tools")

project("xenia-base")
  uuid("aeadaf22-2b20-4941-b05f-a802d5679c11")
  kind("StaticLib")
  language("C++")
  defines({
  })
  includedirs({
    project_root.."/build_tools/third_party/gflags/src",
  })
  local_platform_files()
  removefiles({"main_*.cc"})
  files({
    "debug_visualizers.natvis",
  })

test_suite("xenia-base-tests", project_root, ".", {
  includedirs = {
    project_root.."/build_tools/third_party/gflags/src",
  },
  links = {
    "xenia-base",
  },
})
