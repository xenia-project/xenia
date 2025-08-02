project_root = "../../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-ui-metal")
  uuid("f5a4b3c2-d1e8-4567-9abc-def123456789")
  kind("StaticLib")
  language("C++")
  links({
    "xenia-base",
    "xenia-ui",
    "metal-cpp",
  })
  includedirs({
    project_root.."/third_party/metal-cpp",
  })
  local_platform_files()
  files({
    "../shaders/bytecode/metal/*.h",
  })

  filter("system:macosx")
    -- Add Metal framework dependencies
    links({
      "Metal.framework",
      "MetalKit.framework",
      "QuartzCore.framework",
    })
    -- Add .mm files explicitly for Objective-C++ support
    files({
      "metal_immediate_drawer.mm",
      "metal_presenter.mm",
    })

  filter("not system:macosx")
    -- Exclude Metal UI backend on non-macOS platforms
    removefiles("**")

group("demos")
project("xenia-ui-window-metal-demo")
  uuid("c1d2e3f4-5a6b-7c8d-9e0f-123456789abc")
  single_library_windowed_app_kind()
  language("C++")
  links({
    "fmt",
    "imgui",
    "xenia-base",
    "xenia-ui",
    "xenia-ui-metal",
    "metal-cpp",
  })
  includedirs({
    project_root.."/third_party/metal-cpp",
  })
  files({
    "../window_demo.cc",
    "metal_window_demo.cc",
    project_root.."/src/xenia/ui/windowed_app_main_"..platform_suffix..".cc",
  })
  resincludedirs({
    project_root,
  })

  filter("system:macosx")
    -- Add Metal framework dependencies
    links({
      "Metal.framework",
      "MetalKit.framework", 
      "QuartzCore.framework",
    })
    -- Explicitly include Metal UI backend files
    files({
      "metal_api.cc",
      "metal_immediate_drawer.mm",  -- Objective-C++ file for Metal command encoding
      "metal_immediate_drawer.h",
      "metal_presenter.mm",  -- Objective-C++ file for Metal layer configuration
      "metal_presenter.h",
      "metal_provider.cc",
      "metal_provider.h",
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
      ["PRODUCT_BUNDLE_IDENTIFIER"] = "com.xenia.ui-metal-demo",
      ["CODE_SIGN_STYLE"] = "Automatic",
      ["CODE_SIGN_ENTITLEMENTS"] = path.getabsolute(project_root.."/xenia.entitlements"),
    })

  filter("not system:macosx")
    -- Exclude Metal UI demo on non-macOS platforms
    removefiles("**")
