project_root = "../../.."
include(project_root.."/build_tools")

group("src")
project("xenia-app")
  uuid("d7e98620-d007-4ad8-9dbd-b47c8853a17f")
  kind("WindowedApp")
  targetname("xenia")
  language("C++")
  links({
    "beaengine",
    "elemental-forms",
    "gflags",
    "xenia-apu",
    "xenia-apu-nop",
    "xenia-apu-xaudio2",
    "xenia-base",
    "xenia-core",
    "xenia-cpu",
    "xenia-cpu-backend-x64",
    "xenia-debug",
    "xenia-gpu",
    "xenia-gpu-gl4",
    "xenia-hid-nop",
    "xenia-hid-winkey",
    "xenia-hid-xinput",
    "xenia-kernel",
    "xenia-ui",
    "xenia-ui-gl",
    "xenia-vfs",
  })
  flags({
    "WinMain",  -- Use WinMain instead of main.
  })
  defines({
  })
  includedirs({
    project_root.."/third_party/elemental-forms/src",
  })
  local_platform_files()
  files({
    "xenia_main.cc",
    "../base/main_"..platform_suffix..".cc",
  })
  files({
    "main_resources.rc",
  })
  resincludedirs({
    project_root,
    project_root.."/third_party/elemental-forms",
  })

  filter("configurations:Checked")
    local libav_root = "../third_party/libav-xma-bin/lib/Debug"
    linkoptions({
      libav_root.."/libavcodec.a",
      libav_root.."/libavutil.a",
    })
  filter("configurations:Debug or Release")
    local libav_root = "../third_party/libav-xma-bin/lib/Release"
    linkoptions({
      libav_root.."/libavcodec.a",
      libav_root.."/libavutil.a",
    })

  filter("platforms:Windows")
    -- Only create the .user file if it doesn't already exist.
    local user_file = project_root.."/build/xenia-app.vcxproj.user"
    if not os.isfile(user_file) then
      debugdir(project_root)
      debugargs({
        "--flagfile=scratch/flags.txt",
        "2>&1",
        "1>scratch/stdout.txt",
      })
    end
