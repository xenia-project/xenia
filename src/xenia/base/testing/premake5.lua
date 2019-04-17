project_root = "../../../.."
include(project_root.."/tools/build")

test_suite("xenia-base-tests", project_root, ".", {
  links = {
    "xenia-base",
  },
})
