group("third_party")
project("vulkan-loader")
  uuid("07d77359-1618-43e6-8a4a-0ee9ddc5fa6a")
  kind("StaticLib")
  language("C++")

  defines({
    "_LIB",
    "API_NAME=\"vulkan\"",
  })
  removedefines({
    "_UNICODE",
    "UNICODE",
  })
  includedirs({
    ".",
  })
  recursive_platform_files()

  filter("platforms:Windows")
    warnings("Off")  -- Too many warnings.
    characterset("MBCS")
    defines({
      "VK_USE_PLATFORM_WIN32_KHR",
    })
