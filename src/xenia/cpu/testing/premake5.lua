project_root = "../../../.."
include(project_root.."/tools/build")

test_suite("xenia-cpu-tests", project_root, ".", {
  links = {
    "capstone",
    "xenia-base",
    "xenia-core",
    "xenia-cpu",
    "xenia-cpu-backend-x64",

    -- TODO(benvanik): cut these dependencies?
    "xenia-kernel",
    "xenia-ui", -- needed by xenia-base
  },
})
