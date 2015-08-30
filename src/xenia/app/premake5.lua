project_root = "../../.."
include(project_root.."/build_tools")

group("src")
project("xenia-app")
  uuid("d7e98620-d007-4ad8-9dbd-b47c8853a17f")
  kind("WindowedApp")
  targetname("xenia")
  language("C++")
  links({
    "elemental-forms",
    "gflags",
    "xenia-apu",
    "xenia-apu-nop",
    "xenia-base",
    "xenia-core",
    "xenia-cpu",
    "xenia-cpu-backend-x64",
    "xenia-debug",
    "xenia-gpu",
    "xenia-gpu-gl4",
    "xenia-hid-nop",
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
    project_root.."/build_tools/third_party/gflags/src",
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

  filter("platforms:Windows")
    links({
      "xenia-apu-xaudio2",
      "xenia-hid-winkey",
      "xenia-hid-xinput",
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
