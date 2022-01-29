project_root = "../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-gpu")
  uuid("0e8d3370-e4b1-4b05-a2e8-39ebbcdf9b17")
  kind("StaticLib")
  language("C++")
  links({
    "dxbc",
    "fmt",
    "glslang-spirv",
    "snappy",
    "spirv-tools",
    "xenia-base",
    "xenia-ui",
    "xenia-ui-spirv",
    "xxhash",
  })
  defines({
  })
  includedirs({
    project_root.."/third_party/spirv-tools/external/include",
  })
  local_platform_files()
  -- local_platform_files("spirv")
  -- local_platform_files("spirv/passes")

group("src")
project("xenia-gpu-shader-compiler")
  uuid("ad76d3e4-4c62-439b-a0f6-f83fcf0e83c5")
  kind("ConsoleApp")
  language("C++")
  links({
    "dxbc",
    "fmt",
    "glslang-spirv",
    "snappy",
    "spirv-tools",
    "xenia-base",
    "xenia-gpu",
    "xenia-ui",
    "xenia-ui-spirv",
  })
  defines({
  })
  files({
    "shader_compiler_main.cc",
    "../base/console_app_main_"..platform_suffix..".cc",
  })

  filter("platforms:Windows")
    -- Only create the .user file if it doesn't already exist.
    local user_file = project_root.."/build/xenia-gpu-shader-compiler.vcxproj.user"
    if not os.isfile(user_file) then
      debugdir(project_root)
      debugargs({
        "2>&1",
        "1>scratch/stdout-shader-compiler.txt",
      })
    end
