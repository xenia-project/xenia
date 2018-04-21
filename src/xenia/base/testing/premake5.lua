project_root = "../../../.."
include(project_root.."/tools/build")

test_suite("xenia-base-tests", project_root, ".", {
  includedirs = {
    project_root.."/third_party/gflags/src",
  },
  links = {
    "xenia-base",
  },
})
