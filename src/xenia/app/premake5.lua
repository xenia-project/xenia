project_root = "../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-app")
  uuid("d7e98620-d007-4ad8-9dbd-b47c8853a17f")
  language("C++")
  local metal_converter_libdir =
      path.getabsolute(path.join(project_root, "third_party/metal-shader-converter/lib"))
  links({
    "xenia-apu",
    "xenia-apu-nop",
    "xenia-base",
    "xenia-core",
    "xenia-cpu",
    "xenia-gpu",
    "xenia-gpu-null",
    "xenia-gpu-vulkan",
    "xenia-hid",
    "xenia-hid-nop",
    "xenia-kernel",
    "xenia-ui",
    "xenia-ui-vulkan",
    "xenia-vfs",
  })
  links({
    "aes_128",
    "capstone",
    "fmt",
    "dxbc",
    "discord-rpc",
    "glslang-spirv",
    "imgui",
    "libavcodec",
    "libavutil",
    "mspack",
    "snappy",
    "xxhash",
  })
  defines({
    "XBYAK_NO_OP_NAMES",
    "XBYAK_ENABLE_OMITTED_OPERAND",
  })
  local_platform_files()
  files({
    "../base/main_init_"..platform_suffix..".cc",
    "../ui/windowed_app_main_"..platform_suffix..".cc",
  })

  resincludedirs({
    project_root,
  })

  filter(SINGLE_LIBRARY_FILTER)
    -- Unified library containing all apps as StaticLibs, not just the main
    -- emulator windowed app.
    kind("SharedLib")
    links({
      "xenia-gpu-vulkan-trace-viewer",
      "xenia-hid-demo",
      "xenia-ui-window-vulkan-demo",
    })
  filter(NOT_SINGLE_LIBRARY_FILTER)
    kind("WindowedApp")

  -- `targetname` is broken if building from Gradle, works only for toggling the
  -- `lib` prefix, as Gradle uses LOCAL_MODULE_FILENAME, not a derivative of
  -- LOCAL_MODULE, to specify the targets to build when executing ndk-build.
  filter("platforms:not Android-*")
    targetname("xenia")

  filter("architecture:x86_64")
    links({
      "xenia-cpu-backend-x64",
    })

  filter("architecture:ARM64")
    links({
      "xenia-cpu-backend-a64",
    })

  -- TODO(Triang3l): The emulator itself on Android.
  filter("platforms:not Android-*")
    files({
      "xenia_main.cc",
    })

  filter("platforms:Windows-*")
    files({
      "main_resources.rc",
    })

  filter({"platforms:Windows-*", "architecture:x86_64", "files:../base/main_init_"..platform_suffix..".cc"})
    -- MSVC x64 doesn't support /arch:IA32; remove AVX so the AVX check
    -- implementation itself doesn't get compiled with AVX enabled.
    removebuildoptions({
      "/arch:AVX",
      "/arch:AVX2",
    })

  filter("platforms:not Android-*")
    links({
      "xenia-apu-sdl",
      -- TODO(Triang3l): CPU debugger on Android.
      "xenia-debug-ui",
      "xenia-helper-sdl",
      "xenia-hid-sdl",
    })

  filter("platforms:Mac")
    -- Use the mac-specific windowed app entrypoint (avoid posix stub).
    removefiles({ "../ui/windowed_app_main_posix.cc" })
    files({ "../ui/windowed_app_main_mac.cc" })
    -- Disable Discord RPC on macOS (Windows-only binary).
    removelinks({
      "discord-rpc",
    })

  filter("platforms:Linux")
    links({
      "X11",
      "xcb",
      "X11-xcb",
      "SDL2",
    })

  filter("platforms:Windows-*")
    links({
      "xenia-app-discord",
      "xenia-apu-xaudio2",
      "xenia-gpu-d3d12",
      "xenia-hid-winkey",
      "xenia-hid-xinput",
      "xenia-ui-d3d12",
      -- Windows system libraries needed by dependencies.
      "dxguid",
      "ws2_32",
      -- SDL2 is built as a static library on Windows.
      "SDL2",
      "setupapi",
      "winmm",
      "imm32",
      "version",
    })

  filter({"platforms:Windows-*", SINGLE_LIBRARY_FILTER})
    links({
      "xenia-gpu-d3d12-trace-viewer",
      "xenia-ui-window-d3d12-demo",
    })

  filter("platforms:Windows-*")
    -- Only create the .user file if it doesn't already exist.
    local user_file = project_root.."/build/xenia-app.vcxproj.user"
    if not os.isfile(user_file) then
      debugdir(project_root)
      debugargs({
      })
    end

  filter("platforms:Mac")
    -- Link Metal UI/GPU on macOS so the Metal backend can be selected.
    links({
      "xenia-gpu-metal",
      "xenia-ui-metal",
      "metal-cpp",
      "metalirconverter",
      "SDL2",
      "Metal.framework",
      "MetalKit.framework",
      "QuartzCore.framework",
    })
    libdirs({
      metal_converter_libdir,
      "/usr/local/lib",
    })
    runpathdirs({
      "@executable_path/../Frameworks",
      metal_converter_libdir,
      "/usr/local/lib",
    })
    linkoptions({
      "-Wl,-rpath,@executable_path/../Frameworks",
      "-Wl,-rpath,@loader_path/../Frameworks",
    })
    -- Bundle the Metal shader converter runtime inside the app bundle (Contents/Frameworks).
    postbuildcommands({
      'mkdir -p "${TARGET_BUILD_DIR}/xenia.app/Contents/Frameworks"',
      'cp -f "' ..
          path.join(metal_converter_libdir, "libmetalirconverter.dylib") ..
          '" "${TARGET_BUILD_DIR}/xenia.app/Contents/Frameworks/"'
    })
    files({
      "Info.plist",
      project_root.."/xenia.entitlements",
    })
    buildoptions({
      "-DINFOPLIST_FILE=" .. path.getabsolute("Info.plist"),
    })
    xcodebuildsettings({
      ["INFOPLIST_FILE"] = path.getabsolute("Info.plist"),
      ["PRODUCT_BUNDLE_IDENTIFIER"] = "com.xenia.ui-vulkan-demo",
      ["CODE_SIGN_STYLE"] = "Automatic",
      ["CODE_SIGN_ENTITLEMENTS"] = path.getabsolute(project_root.."/xenia.entitlements"),
    })