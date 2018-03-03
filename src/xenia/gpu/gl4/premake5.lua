project_root = "../../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-gpu-gl4")
  uuid("da10149d-efb0-44aa-924c-a76a46e1f04d")
  kind("StaticLib")
  language("C++")
  links({
    "glew",
    "xenia-base",
    "xenia-gpu",
    "xenia-ui",
    "xenia-ui-gl",
    "xxhash",
  })
  defines({
    "GLEW_STATIC=1",
    "GLEW_MX=1",
  })
  includedirs({
    project_root.."/third_party/gflags/src",
  })
  local_platform_files()

-- TODO(benvanik): kill this and move to the debugger UI.
group("src")
project("xenia-gpu-gl4-trace-viewer")
  uuid("450f965b-a019-4ba5-bc6f-99901e5a4c8d")
  kind("WindowedApp")
  language("C++")
  links({
    "capstone",
    "gflags",
    "glew",
    "imgui",
    "libavcodec",
    "libavutil",
    "snappy",
    "xenia-apu",
    "xenia-apu-nop",
    "xenia-base",
    "xenia-core",
    "xenia-cpu",
    "xenia-cpu-backend-x64",
    "xenia-gpu",
    "xenia-gpu-gl4",
    "xenia-hid",
    "xenia-hid-nop",
    "xenia-kernel",
    "xenia-ui",
    "xenia-ui-gl",
    "xenia-vfs",
    "xxhash",
  })
  flags({
    "WinMain",  -- Use WinMain instead of main.
  })
  defines({
    "GLEW_STATIC=1",
    "GLEW_MX=1",
  })
  includedirs({
    project_root.."/third_party/gflags/src",
  })
  files({
    "gl4_trace_viewer_main.cc",
    "../../base/main_"..platform_suffix..".cc",
  })

  filter("platforms:Windows")
    links({
      "xenia-apu-xaudio2",
      "xenia-hid-winkey",
      "xenia-hid-xinput",
    })

    -- Only create the .user file if it doesn't already exist.
    local user_file = project_root.."/build/xenia-gpu-gl4-trace-viewer.vcxproj.user"
    if not os.isfile(user_file) then
      debugdir(project_root)
      debugargs({
        "--flagfile=scratch/flags.txt",
        "2>&1",
        "1>scratch/stdout-trace-viewer.txt",
      })
    end
  