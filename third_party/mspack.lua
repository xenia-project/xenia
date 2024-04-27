group("third_party")
project("mspack")
  uuid("0881692A-75A1-4E7B-87D8-BB9108CEDEA4")
  kind("StaticLib")
  language("C++")
  links({
    "xenia-base",
  })
  defines({
    "_LIB",
    "HAVE_CONFIG_H",
  })
  removedefines({
    "_UNICODE",
    "UNICODE",
  })
  includedirs({
      "mspack",
  })
  files({
      "mspack/logging.cc",
      "mspack/lzx.h",
      "mspack/lzxd.c",
      "mspack/mspack.h",
      "mspack/readbits.h",
      "mspack/readhuff.h",
      "mspack/system.c",
      "mspack/system.h",
  })

  filter("platforms:Windows-*")
    defines({
    })
  filter("platforms:Linux")
    defines({
    })
