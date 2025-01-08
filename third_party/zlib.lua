group("third_party")
project("zlib")
  uuid("13d4073d-f1c9-47e3-a057-79b133596fc2")
  kind("StaticLib")
  language("C")
  cdialect("C90")
  filter({"toolset:gcc or clang"})
    buildoptions({
      "-Wno-implicit-function-declaration"
    })
  filter {}

  files({
    "zlib/*.c",
    "zlib/*.h",
  })