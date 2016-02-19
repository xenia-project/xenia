group("third_party")
project("glslang-spirv")
  uuid("1cc8f45e-91e2-4daf-a55e-666bf8b5e6b2")
  kind("StaticLib")
  language("C++")
  links({
  })
  defines({
    "_LIB",
  })
  includedirs({
  })
  files({
    "glslang-spirv/disassemble.cpp",
    "glslang-spirv/disassemble.h",
    "glslang-spirv/doc.cpp",
    "glslang-spirv/doc.h",
    "glslang-spirv/GLSL.std.450.h",
    -- Disabled until required.
    -- "glslang-spirv/GlslangToSpv.cpp",
    -- "glslang-spirv/GlslangToSpv.h",
    "glslang-spirv/InReadableOrder.cpp",
    "glslang-spirv/spirv.hpp",
    "glslang-spirv/SpvBuilder.cpp",
    "glslang-spirv/SpvBuilder.h",
    "glslang-spirv/spvIR.h",
    "glslang-spirv/SPVRemapper.cpp",
    "glslang-spirv/SPVRemapper.h",
  })
