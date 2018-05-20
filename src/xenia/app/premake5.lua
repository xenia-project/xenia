project_root = "../../.."
include(project_root.."/tools/build")
local qt = premake.extensions.qt

group("src")
project("xenia-app")
  uuid("d7e98620-d007-4ad8-9dbd-b47c8853a17f")
  kind("WindowedApp")
  targetname("xenia")
  language("C++")
  links({
    "capstone",
    "gflags",
    "glew",
    "glslang-spirv",
    "imgui",
    "libavcodec",
    "libavutil",
    "snappy",
    "spirv-tools",
    "volk",
    "xenia-apu",
    "xenia-apu-nop",
    "xenia-base",
    "xenia-core",
    "xenia-cpu",
    "xenia-cpu-backend-x64",
    "xenia-debug-ui",
    "xenia-gpu",
    "xenia-gpu-null",
    "xenia-gpu-vulkan",
    "xenia-hid",
    "xenia-hid-nop",
    "xenia-kernel",
    "xenia-ui",
    -- "xenia-ui-qt",
    "xenia-ui-spirv",
    "xenia-ui-vulkan",
    "xenia-vfs",
    "xxhash",
  })

  -- Setup Qt libraries
  qt.enable()
  qtmodules{"core", "gui", "widgets"}
  qtpath(qt.defaultpath)
  qtprefix "Qt5"
  configuration {"Checked"}
    qtsuffix "d"
  configuration {}

  -- Qt static configuration (if necessary). Used by AppVeyor.
  if os.getenv("QT_STATIC") then
    qt.modules["AccessibilitySupport"] = {
      name = "AccessibilitySupport",
      include = "QtAccessibilitySupport",
    }
    qt.modules["EventDispatcherSupport"] = {
      name = "EventDispatcherSupport",
      include = "QtEventDispatcherSupport",
    }
    qt.modules["FontDatabaseSupport"] = {
      name = "FontDatabaseSupport",
      include = "QtFontDatabaseSupport",
    }
    qt.modules["ThemeSupport"] = {
      name = "ThemeSupport",
      include = "QtThemeSupport",
    }
    qt.modules["VulkanSupport"] = {
      name = "VulkanSupport",
      include = "QtVulkanSupport",
    }

    configuration {"not Checked"}
      links({
        "qtmain",
        "qtfreetype",
        "qtlibpng",
        "qtpcre2",
        "qtharfbuzz",
      })
    configuration {"Checked"}
      links({
        "qtmaind",
        "qtfreetyped",
        "qtlibpngd",
        "qtpcre2d",
        "qtharfbuzzd",
      })
    configuration {}
    qtmodules{"AccessibilitySupport", "EventDispatcherSupport", "FontDatabaseSupport", "ThemeSupport", "VulkanSupport"}
    libdirs("%{cfg.qtpath}/plugins/platforms")

    filter("platforms:Windows")
      -- Qt dependencies
      links({
        "dwmapi",
        "version",
        "imm32",
        "winmm",
        "netapi32",
        "userenv",
      })
      configuration {"not Checked"}
        links({"qwindows"})
      configuration {"Checked"}
        links({"qwindowsd"})
      configuration {}
    filter()
  end

  flags({
    "WinMain",  -- Use WinMain instead of main.
  })
  defines({
  })
  includedirs({
    project_root.."/third_party/gflags/src",
  })
  local_platform_files()
  files({
    "xenia_main.cc",
    "../base/main_"..platform_suffix..".cc",

    -- Qt files
    "*.qrc",
  })
  filter("platforms:Windows")
    resincludedirs({
      project_root,
    })

  filter("platforms:Linux")
    links({
      "X11",
      "xcb",
      "X11-xcb",
      "GL",
      "vulkan",
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
      debugenvs({
        "PATH=" .. qt.defaultpath .. "/bin",
      })
    end
