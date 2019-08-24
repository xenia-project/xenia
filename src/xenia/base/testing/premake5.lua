project_root = "../../../.."
include(project_root.."/tools/build")

test_suite("xenia-base-tests", project_root, ".", {
  links = {
    "fmt",
    "xenia-base",
  },
})
  files({
    "res/*",
  })
  filter("files:res/*")
    flags({"ExcludeFromBuild"})
  filter("platforms:Windows")
    debugdir(project_root)