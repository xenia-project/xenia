project_root = "../../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-gpu-d3d12")
  uuid("c057eae4-e7bb-4113-9a69-1fe07b735c49")
  kind("StaticLib")
  language("C++")
  links({
    "fmt",
    "xenia-base",
    "xenia-gpu",
    "xenia-ui",
    "xenia-ui-d3d12",
    "xxhash",
  })
  local_platform_files()
  files({
    "../shaders/bytecode/d3d12_5_1/*.h",
  })

group("src")
project("xenia-gpu-d3d12-trace-viewer")
  uuid("7b5b9fcb-7bf1-43ff-a774-d4c41c8706be")
  single_library_windowed_app_kind()
  language("C++")
  links({
    "xenia-apu",
    "xenia-apu-nop",
    "xenia-base",
    "xenia-core",
    "xenia-cpu",
    "xenia-gpu",
    "xenia-gpu-d3d12",
    "xenia-hid",
    "xenia-hid-nop",
    "xenia-kernel",
    "xenia-ui",
    "xenia-ui-d3d12",
    "xenia-vfs",
  })
  links({
    "aes_128",
    "capstone",
    "dxbc",
    "fmt",
    "imgui",
    "libavcodec",
    "libavutil",
    "mspack",
    "snappy",
    "xxhash",
  })
  files({
    "d3d12_trace_viewer_main.cc",
    "../../ui/windowed_app_main_"..platform_suffix..".cc",
  })
  -- Only create the .user file if it doesn't already exist.
  local user_file = project_root.."/build/xenia-gpu-d3d12-trace-viewer.vcxproj.user"
  if not os.isfile(user_file) then
    debugdir(project_root)
    debugargs({
      "2>&1",
      "1>scratch/stdout-trace-viewer.txt",
    })
  end

  filter("architecture:x86_64")
    links({
      "xenia-cpu-backend-x64",
    })

  filter("architecture:ARM64")
    links({
      "xenia-cpu-backend-a64",
    })

group("src")
project("xenia-gpu-d3d12-trace-dump")
  uuid("686b859c-0046-44c4-a02c-41fc3fb75698")
  kind("ConsoleApp")
  language("C++")
  links({
    "xenia-apu",
    "xenia-apu-nop",
    "xenia-base",
    "xenia-core",
    "xenia-cpu",
    "xenia-gpu",
    "xenia-gpu-d3d12",
    "xenia-hid",
    "xenia-hid-nop",
    "xenia-kernel",
    "xenia-ui",
    "xenia-ui-d3d12",
    "xenia-vfs",
  })
  links({
    "aes_128",
    "capstone",
    "dxbc",
    "fmt",
    "imgui",
    "libavcodec",
    "libavutil",
    "mspack",
    "snappy",
    "xxhash",
  })
  files({
    "d3d12_trace_dump_main.cc",
    "../../base/console_app_main_"..platform_suffix..".cc",
  })
  -- Only create the .user file if it doesn't already exist.
  local user_file = project_root.."/build/xenia-gpu-d3d12-trace-dump.vcxproj.user"
  if not os.isfile(user_file) then
    debugdir(project_root)
    debugargs({
      "2>&1",
      "1>scratch/stdout-trace-dump.txt",
    })
  end

  filter("architecture:x86_64")
    links({
      "xenia-cpu-backend-x64",
    })

  filter("architecture:ARM64")
    links({
      "xenia-cpu-backend-a64",
    })
