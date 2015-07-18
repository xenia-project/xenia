project_root = "../../../.."
include(project_root.."/build_tools")

test_suite("xenia-cpu-tests", project_root, ".", {
  includedirs = {
  },
  links = {
    "xenia-base",
    "xenia-core",
    "xenia-cpu",
    "xenia-cpu-backend-x64",

    -- TODO(benvanik): cut these dependencies?
    "xenia-debug",
    "xenia-kernel",
  },
})
