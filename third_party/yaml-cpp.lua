group("third_party")
project("yaml-cpp")
  uuid("47bfe853-a3f8-4902-921d-d564608ff355")
  kind("StaticLib")
  language("C++")

  defines({
    "_LIB",
  })
  includedirs({
    "yaml-cpp/include/",
  })
  recursive_platform_files("yaml-cpp/include/yaml-cpp")
  recursive_platform_files("yaml-cpp/src")

  filter("platforms:Windows")
    warnings("Off")  -- Too many warnings.
