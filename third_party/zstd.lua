include("third_party/zstd/contrib/premake/zstd.lua")

group("third_party")
project("zstd")
  uuid("df336aac-f0c8-11ed-a05b-0242ac120003")
  -- TODO: https://github.com/facebook/zstd/issues/3278
  --defines({
  --  "ZSTD_DISABLE_ASM",
  --})
  project_zstd("zstd/lib/")
