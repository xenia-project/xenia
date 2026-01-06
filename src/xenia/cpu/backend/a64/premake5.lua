project_root = "../../../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-cpu-backend-a64")
  uuid("495f3f3e-f5e8-489a-bd0f-289d0495bc08")
  filter("architecture:ARM64")
    kind("StaticLib")
  filter("architecture:not ARM64")
    kind("None")
  filter({})
  language("C++")
  cppdialect("C++20")
  links({
    "fmt",
    "xenia-base",
    "xenia-cpu",
  })
  defines({
  })

  disablewarnings({
    -- Silence errors in oaknut
    "4146", -- unary minus operator applied to unsigned type, result still unsigned
    "4267" -- 'initializing': conversion from 'size_t' to 'uint32_t', possible loss of data
  })

  includedirs({
    project_root.."/third_party/oaknut/include",
  })
  local_platform_files()
