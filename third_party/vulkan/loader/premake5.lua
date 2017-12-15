group("third_party")
project("vulkan-loader")
  uuid("07d77359-1618-43e6-8a4a-0ee9ddc5fa6a")
  kind("StaticLib")
  language("C")

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

  -- Included elsewhere
  removefiles("vk_loader_extensions.c")

  filter("platforms:Windows")
    warnings("Off")  -- Too many warnings.
    characterset("MBCS")
    defines({
      "VK_USE_PLATFORM_WIN32_KHR",
    })
  filter("platforms:not Windows")
    removefiles("dirent_on_windows.c")
  filter("platforms:Linux")
    defines({
      "VK_USE_PLATFORM_XCB_KHR",
      [[SYSCONFDIR="\"/etc\""]],
      [[FALLBACK_CONFIG_DIRS="\"/etc/xdg\""]],
      [[DATADIR="\"/usr/share\""]],
      [[FALLBACK_DATA_DIRS="\"/usr/share:/usr/local/share\""]],
    })
