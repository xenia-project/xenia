project_root = "../.."
include(project_root.."/tools/build")

group("src")
project("xenia-core")
  uuid("970f7892-f19a-4bf5-8795-478c51757bec")
  kind("StaticLib")
  language("C++")
  links({
	"cpptoml",
    "xenia-base",
  })
  defines({
  })
  includedirs({
    project_root.."/third_party/gflags/src",
  })
  files({"*.h", "*.cc"})
