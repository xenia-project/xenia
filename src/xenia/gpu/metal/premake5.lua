-- premake5.lua
local project_root = "../../../.."
local metal_converter_libdir = path.join(project_root, "third_party/metal-shader-converter/lib")

include(path.join(project_root, "tools/build"))

----------------------------------------------------------------------
-- GPU backend -------------------------------------------------------
----------------------------------------------------------------------

group("src")
project("xenia-gpu-metal")
  uuid("a1b2c3d4-e5f6-7890-abcd-ef1234567890")
  kind("StaticLib")
  language("C++")

  links {
    "xenia-base",
    "xenia-gpu",
    "xenia-ui",
    "fmt",
    "metal-cpp",
  }

  filter "system:macosx"
    files {
      "dxbc_to_dxil_converter.cc",
      "dxbc_to_dxil_converter.h",
      "metal_command_processor.cc",
      "metal_command_processor.h",
      "metal_graphics_system.cc",
      "metal_graphics_system.h",
      "metal_shader.cc",
      "metal_shader.h",
      "metal_shared_memory.cc",
      "metal_shared_memory.h",
    }

    includedirs {
      path.join(project_root, "third_party/metal-shader-converter/include"),
      "/usr/local/include/metal_irconverter_runtime"
    }

    defines { "METAL_SHADER_CONVERTER_AVAILABLE" }

    libdirs     { metal_converter_libdir, "/usr/local/lib" }
    runpathdirs { metal_converter_libdir, "/usr/local/lib" }

    links {
      "Metal.framework",
      "MetalKit.framework",
      "metalirconverter",
    }
  filter "not system:macosx"
    removefiles "**"
  filter {}

----------------------------------------------------------------------
-- Trace‑dump utility -----------------------------------------------
----------------------------------------------------------------------

project("xenia-gpu-metal-trace-dump")
  uuid("8e9f0a1b-2c3d-4e5f-6789-0abcdef12345")
  kind("ConsoleApp")
  language("C++")

  links {
    "xenia-apu",
    "xenia-apu-nop",
    "xenia-base",
    "xenia-core",
    "xenia-cpu",
    "xenia-gpu",
    "xenia-gpu-metal",
    "xenia-hid",
    "xenia-hid-nop",
    "xenia-kernel",
    "xenia-ui",
    "xenia-ui-metal",
    "xenia-vfs",

    "aes_128",
    "capstone",
    "fmt",
    "imgui",
    "libavcodec",
    "libavutil",
    "mspack",
    "snappy",
    "xxhash",
    "metal-cpp",
  }

  files {
    "metal_trace_dump_main.cc",
  }
  
  -- Check if mac-specific file exists, otherwise use posix
  local mac_main = path.join("..", "..", "base", "console_app_main_mac.mm")
  local posix_main = path.join("..", "..", "base", "console_app_main_posix.cc")
  
  filter "system:macosx"
    if os.isfile(path.join(project_root, "src/xenia/base/console_app_main_mac.mm")) then
      files { mac_main }
    else
      files { posix_main }
    end
  filter "not system:macosx"
    files { path.join("..", "..", "base", "console_app_main_" .. platform_suffix .. ".cc") }
  filter {}

  filter "architecture:ARM64"
    links { "xenia-cpu-backend-a64" }

  filter "system:macosx"
    libdirs     { metal_converter_libdir, "/usr/local/lib" }
    runpathdirs { metal_converter_libdir, "/usr/local/lib" }
    includedirs {
      path.join(project_root, "third_party/metal-shader-converter/include"),
      "/usr/local/include/metal_irconverter_runtime"
    }

    defines { "METAL_SHADER_CONVERTER_AVAILABLE" }

    links       { "Metal.framework", "MetalKit.framework", "metalirconverter" }
  filter "not system:macosx"
    removefiles "**"
  filter {}
