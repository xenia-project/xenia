include("tools/build")
require("third_party/premake-export-compile-commands/export-compile-commands")
require("third_party/premake-androidndk/androidndk")
require("third_party/premake-cmake/cmake")

location(build_root)
targetdir(build_bin)
objdir(build_obj)

-- Define an ARCH variable
-- Only use this to enable architecture-specific functionality.
if os.istarget("linux") then
  ARCH = os.outputof("uname -p")
else
  ARCH = "unknown"
end

includedirs({
  ".",
  "src",
  "third_party",
})

defines({
  "_UNICODE",
  "UNICODE",
})

cppdialect("C++17")
exceptionhandling("On")
rtti("On")
symbols("On")

-- TODO(DrChat): Find a way to disable this on other architectures.
if ARCH ~= "ppc64" then
  filter("architecture:x86_64")
    vectorextensions("AVX")
  filter({})
end

characterset("Unicode")
flags({
  "FatalWarnings",        -- Treat warnings as errors.
})

filter("kind:StaticLib")
  defines({
    "_LIB",
  })

filter("configurations:Checked")
  runtime("Debug")
  optimize("Off")
  defines({
    "DEBUG",
  })
filter({"configurations:Checked", "platforms:Windows-*"})
  buildoptions({
    "/RTCsu",           -- Full Run-Time Checks.
  })
filter({"configurations:Checked", "platforms:Linux"})
  defines({
    "_GLIBCXX_DEBUG",   -- libstdc++ debug mode
  })

filter("configurations:Debug")
  runtime("Release")
  optimize("Off")
  defines({
    "DEBUG",
    "_NO_DEBUG_HEAP=1",
  })
filter({"configurations:Debug", "platforms:Linux"})
  defines({
    "_GLIBCXX_DEBUG",   -- make dbg symbols work on some distros
  })

filter("configurations:Release")
  runtime("Release")
  defines({
    "NDEBUG",
    "_NO_DEBUG_HEAP=1",
  })
  optimize("Speed")
  inlining("Auto")
  flags({
    "LinkTimeOptimization",
  })
  -- Not using floatingpoint("Fast") - NaN checks are used in some places
  -- (though rarely), overall preferable to avoid any functional differences
  -- between debug and release builds, and to have calculations involved in GPU
  -- (especially anything that may affect vertex position invariance) and CPU
  -- (such as constant propagation) emulation as predictable as possible,
  -- including handling of specials since games make assumptions about them.

-- Mac Stuff
filter("platforms:Mac")
  system("macosx")
  toolset("clang")
  
  -- Compiler Options
  buildoptions({
    "-Wall",                     -- Enable all warnings
    "-Wextra",                   -- Enable extra warnings
    "-Wno-unused-parameter",     -- Disable specific warnings if necessary
    "-Wno-error",                -- Disable treating warnings as errors
    "-ferror-limit=0"
    -- Add other macOS-specific compiler flags as needed
  })
  
  -- Linker Options
  linkoptions({
    "-v",
    "-L/opt/homebrew/lib",
    "-framework Cocoa",            -- GUI framework
    "-framework IOKit",            -- Hardware interactions
    "-framework CoreFoundation",    -- Fundamental data types and utilities
    "-framework Security",         -- Security and cryptography
    "-framework OpenGL",           -- Graphics rendering (if used)
    "-framework Metal",            -- Modern graphics API (if used)
    "-lc++",                       -- C++ standard library
    "-lpthread",                   -- POSIX threads
    "-llz4",                        -- Compression library
    "--stdlib=libc++",             -- Use libc++ standard library
    "-g",                          -- Debug symbols
    -- Add other macOS-specific linker flags as needed
  })
  
  -- Libraries to Link Against
  links({
    "pthread",
    "lz4",
    "c++",
    -- Remove "stdc++" as macOS uses "libc++" by default
    -- "stdc++",
    -- Add other necessary libraries here
  })
  
  -- Preprocessor Definitions
  defines({
    "__APPLE__",
    "__MACH__",
    -- Add other macOS-specific defines as needed
  })
  
  -- Additional Settings
  flags({
    "NoPCH",            -- Disable Precompiled Headers if not used
    "Symbols",          -- Include debugging symbols
  -- "OptimizeSpeed",  -- Removed to prevent conflict
    -- Add other flags as needed
  })

    -- Optimization Flags per Configuration
    filter("configurations:Debug")
    buildoptions({
      "-O0",  -- Disable optimizations for Debug
    })
  
  filter("configurations:Release")
    optimize("Speed")  -- Enable speed optimizations for Release
  
filter({})

filter({"platforms:Linux", "kind:*App"})
  linkgroups("On")

filter({"platforms:Linux", "language:C++", "toolset:gcc"})
  disablewarnings({
    "unused-result"
  })

filter({"platforms:Linux", "toolset:gcc"})
  if ARCH == "ppc64" then
    buildoptions({
      "-m32",
      "-mpowerpc64"
    })
    linkoptions({
      "-m32",
      "-mpowerpc64"
    })
  end

filter({"platforms:Linux", "language:C++", "toolset:clang"})
  disablewarnings({
    "deprecated-register"
  })
filter({"platforms:Linux", "language:C++", "toolset:clang", "files:*.cc or *.cpp"})
  buildoptions({
    "-stdlib=libstdc++",
  })

-- Create scratch/ path
if not os.isdir("scratch") then
  os.mkdir("scratch")
end

workspace("xenia")
  uuid("931ef4b0-6170-4f7a-aaf2-0fece7632747")
  startproject("xenia-app")
  if os.istarget("android") then
    platforms({"Android-ARM64", "Android-x86_64"})
    filter("platforms:Android-ARM64")
      architecture("ARM64")
    filter("platforms:Android-x86_64")
      architecture("x86_64")
    filter({})
  else
    if os.istarget("linux") then
      platforms({"Linux"})
      architecture("ARM64")
    elseif os.istarget("macosx") then
      platforms({"Mac"})
      architecture("ARM64")  -- Explicitly set architecture
      xcodebuildsettings({           
        ["ARCHS"] = "arm64"
      })
    elseif os.istarget("windows") then
      platforms({"Windows-ARM64", "Windows-x86_64"})
      filter("platforms:Windows-ARM64")
        architecture("ARM64")
      filter("platforms:Windows-x86_64")
        architecture("x86_64")
      filter({})
    end
  end
  configurations({"Checked", "Debug", "Release"})

  include("third_party/aes_128.lua")
  include("third_party/capstone.lua")
  include("third_party/dxbc.lua")
  include("third_party/discord-rpc.lua")
  include("third_party/cxxopts.lua")
  include("third_party/cpptoml.lua")
  include("third_party/FFmpeg/premake5.lua")
  include("third_party/fmt.lua")
  include("third_party/glslang-spirv.lua")
  include("third_party/imgui.lua")
  include("third_party/mspack.lua")
  include("third_party/snappy.lua")
  include("third_party/xxhash.lua")

  if not os.istarget("android") then
    -- SDL2 requires sdl2-config, and as of November 2020 isn't high-quality on
    -- Android yet, most importantly in game controllers - the keycode and axis
    -- enums are being ruined during conversion to SDL2 enums resulting in only
    -- one controller (Nvidia Shield) being supported, digital triggers are also
    -- not supported; lifecycle management (especially surface loss) is also
    -- complicated.
    include("third_party/SDL2.lua")
  end

  -- Disable treating warnings as fatal errors for all third party projects, as
  -- well as other things relevant only to Xenia itself.
  for _, prj in ipairs(premake.api.scope.current.solution.projects) do
    project(prj.name)
    removefiles({
      "src/xenia/base/app_win32.manifest"
    })
    removeflags({
      "FatalWarnings",
    })
  end

  include("src/xenia")
  include("src/xenia/app")
  include("src/xenia/app/discord")
  include("src/xenia/apu")
  include("src/xenia/apu/nop")
  include("src/xenia/base")
  include("src/xenia/cpu")

  filter("architecture:x86_64")
    include("src/xenia/cpu/backend/x64")
  filter("architecture:ARM64")
    include("src/xenia/cpu/backend/a64")
  filter({})

  include("src/xenia/debug/ui")
  include("src/xenia/gpu")
  include("src/xenia/gpu/null")
  include("src/xenia/gpu/vulkan")
  include("src/xenia/hid")
  include("src/xenia/hid/nop")
  include("src/xenia/kernel")
  include("src/xenia/ui")
  include("src/xenia/ui/vulkan")
  include("src/xenia/vfs")

  if not os.istarget("android") then
    include("src/xenia/apu/sdl")
    include("src/xenia/helper/sdl")
    include("src/xenia/hid/sdl")
  end

  if os.istarget("windows") then
    include("src/xenia/apu/xaudio2")
    include("src/xenia/gpu/d3d12")
    include("src/xenia/hid/winkey")
    include("src/xenia/hid/xinput")
    include("src/xenia/ui/d3d12")
  end
