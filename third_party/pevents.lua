group("third_party")
project("pevents")
  uuid("942076cb-96bf-4c56-a350-711b28914fa9")
  kind("StaticLib")
  language("C++")
  links({
  })
  defines({
    "_LIB",
  })
  filter("platforms:Linux")
    includedirs({
      "pevents/src",
    })
    files({
      "pevents/src/pevents.h",
      "pevents/src/pevents.cpp",
    })
