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
    "metalirconverter",
  }

  filter "system:macosx"
    files {
      "metal_command_processor.cc",
      "metal_command_processor.h",
      "metal_graphics_system.cc",
      "metal_graphics_system.h",
      "metal_shader.cc",
      "metal_shader.h",
      "metal_pipeline_cache.cc",
      "metal_pipeline_cache.h",
      "metal_buffer_cache.cc",
      "metal_buffer_cache.h",
      "metal_texture_cache.cc",
      "metal_texture_cache.h",
      "metal_render_target_cache.cc",
      "metal_render_target_cache.h",
      "metal_presenter.cc",
      "metal_presenter.h",
      "metal_primitive_processor.cc",
      "metal_primitive_processor.h",
      "metal_shared_memory.h",
      "dxbc_to_dxil_converter.cc",
      "dxbc_to_dxil_converter.h",
      "metal_shader_cache.cc",
      "metal_shader_cache.h",
    }

    libdirs     { metal_converter_libdir }
    runpathdirs { metal_converter_libdir }

    links {
      "Metal.framework",
      "MetalKit.framework",
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
    "metalirconverter",
  }

  files {
    "metal_trace_dump_main.cc",
    path.join("..", "..", "base", "console_app_main_" .. platform_suffix .. ".cc"),
  }

  filter "architecture:ARM64"
    links { "xenia-cpu-backend-a64" }

  filter "system:macosx"
    libdirs     { metal_converter_libdir }
    runpathdirs { metal_converter_libdir }
    links       { "Metal.framework", "MetalKit.framework" }
  filter "not system:macosx"
    removefiles "**"
  filter {}
