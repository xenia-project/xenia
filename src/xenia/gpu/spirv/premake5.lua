project_root = "../../../.."
include(project_root.."/build_tools")

group("src")
project("xenia-gpu-spirv")
  uuid("e8a9f997-39ff-4ae2-803f-937525ad4bfb")
  kind("StaticLib")
  language("C++")
  links({
    "xenia-base",
    "xenia-gpu",
  })
  defines({
  })
  includedirs({
    project_root.."/build_tools/third_party/gflags/src",
  })
  local_platform_files()

group("src")
project("xenia-gpu-spirv-compiler")
  uuid("ad76d3e4-4c62-439b-a0f6-f83fcf0e83c5")
  kind("ConsoleApp")
  language("C++")
  links({
    "gflags",
    "xenia-base",
    "xenia-gpu",
    "xenia-gpu-spirv",
  })
  defines({
  })
  includedirs({
    project_root.."/build_tools/third_party/gflags/src",
  })
  files({
    "spirv_compiler_main.cc",
    "../../base/main_"..platform_suffix..".cc",
  })

  filter("platforms:Windows")
    -- Only create the .user file if it doesn't already exist.
    local user_file = project_root.."/build/xenia-gpu-spirv-compiler.vcxproj.user"
    if not os.isfile(user_file) then
      debugdir(project_root)
      debugargs({
        "--flagfile=scratch/flags.txt",
        "2>&1",
        "1>scratch/stdout-spirv-compiler.txt",
      })
    end
