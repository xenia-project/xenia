include("third_party/zstd/contrib/premake/zstd.lua")

group("third_party")
project("zstd")
  uuid("df336aac-f0c8-11ed-a05b-0242ac120003")
  -- https://gitlab.kitware.com/cmake/cmake/-/issues/25744
  filter({"toolset:not msc"})
    defines({
      "ZSTD_DISABLE_ASM",
    })
  project_zstd("zstd/lib/")
