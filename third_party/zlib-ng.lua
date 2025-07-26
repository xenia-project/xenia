group("third_party")
project("zlib-ng")
  uuid("13d4073d-f1c9-47e3-a057-79b133596fc2")
  kind("StaticLib")
  language("C")

  defines({
    "X86_FEATURES",
    "X86_HAVE_XSAVE_INTRIN",
    "X86_SSSE3",
    "X86_SSE42",
    "WITH_GZFILEOP",
  })
  if os.istarget("windows") then
    defines({
      "X86_SSE2",
      "X86_AVX2",
      "X86_AVX512",
      "X86_AVX512VNNI",
      "X86_PCLMULQDQ_CRC",
      "X86_VPCLMULQDQ_CRC",
    })
  end

  files({
    "zlib-ng/*.c",
    "zlib-ng/arch/x86/*.c",
    "zlib-ng/arch/generic/*.c",
  })
  if not os.istarget("windows") then
    removefiles({
      "zlib-ng/arch/x86/adler32_avx2.c",
      "zlib-ng/arch/x86/adler32_avx512.c",
      "zlib-ng/arch/x86/adler32_avx512_vnni.c",
      "zlib-ng/arch/x86/chunkset_avx2.c",
      "zlib-ng/arch/x86/compare256_avx2.c",
      "zlib-ng/arch/x86/crc32_pclmulqdq.c",
      "zlib-ng/arch/x86/crc32_vpclmulqdq.c",
      "zlib-ng/arch/x86/slide_hash_avx2.c",
    })
  end

  includedirs({
    "zlib-ng",
  })

  local zlibng_dir = path.getabsolute("zlib-ng")
  local zlibng_h_src_files = {
    path.join(zlibng_build_dir, "zlib-ng.h"),
    path.join(zlibng_build_dir, "zconf-ng.h"),
    path.join(zlibng_build_dir, "zlib_name_mangling-ng.h"),
    path.join(zlibng_build_dir, "gzread.c"),
  }
  for i = 1,#zlibng_h_src_files,1
  do
    if not os.isfile(path.join(zlibng_dir, zlibng_h_src_files[i])) then
      zlibng_build = true
      break
    end
  end
  if zlibng_build then
    os.execute("cmake -DZLIB_ENABLE_TESTS=OFF -DWITH_GTEST=OFF "..zlibng_dir.." -B"..zlibng_dir)
  end
