project_root = "../../../.."
include(project_root.."/tools/build")

test_suite("xenia-vfs-tests", project_root, ".", {
  links = {
    "lz4",
    "xenia-vfs",
  },
})
