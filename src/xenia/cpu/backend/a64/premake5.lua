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

    -- Add oaknut as external include to suppress warnings
    filter("toolset:clang or toolset:gcc")
      externalincludedirs({
        project_root.."/third_party/oaknut/include",
      })
      -- Also explicitly disable the warning for third-party code
      buildoptions({
        "-Wno-shorten-64-to-32",
      })
    filter("toolset:msc")
      includedirs({
        project_root.."/third_party/oaknut/include",
      })
    filter("architecture:ARM64")
    
    -- Include only ARM64-specific files
    local_platform_files()

  -- For non-ARM64 architectures, create an empty static lib
  filter("architecture:x86_64")
      kind("None")

  -- Reset filter
  filter({})
