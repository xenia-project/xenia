project_root = "../../../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-cpu-backend-a64")
  uuid("495f3f3e-f5e8-489a-bd0f-289d0495bc08")
  
  -- Apply settings only for ARM64
  filter("architecture:ARM64")
    kind("StaticLib")
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
      "4146", -- unary minus operator applied to unsigned type
      "4267"  -- conversion from 'size_t' to 'uint32_t'
    })

    includedirs({
      project_root.."/third_party/oaknut/include",
    })
    
    -- Include only ARM64-specific files
    local_platform_files()

  -- For non-ARM64 architectures, set the project kind to `None`
  filter("architecture:not ARM64")
    kind("None")

  -- Reset filter
  filter({})
