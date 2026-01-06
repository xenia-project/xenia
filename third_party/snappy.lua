group("third_party")
project("snappy")
  uuid("bb143d61-3fd4-44c2-8b7e-04cc538ba2c7")
  kind("StaticLib")
  language("C++")

  defines({
    "_LIB",
  })
  files({
    "snappy/snappy-internal.h",
    "snappy/snappy-sinksource.cc",
    "snappy/snappy-sinksource.h",
    "snappy/snappy-stubs-internal.cc",
    "snappy/snappy-stubs-internal.h",
    "snappy/snappy-stubs-public.h",
    "snappy/snappy.cc",
    "snappy/snappy.h",
  })

  filter("platforms:Windows-*")
    warnings("Off")  -- Too many warnings.
