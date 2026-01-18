project_root = "../../../.."
include(project_root.."/tools/build")

test_suite("xenia-vfs-tests", project_root, ".", {
  links = {
    "fmt",
    "xenia-base",
    "xenia-vfs",
  },
})
