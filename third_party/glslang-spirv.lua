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
--  filter({"configurations:Release", "platforms:Windows"})
--    buildoptions({
--      "/O1",
--    })
--  filter {}

  files({
    "glslang/SPIRV/bitutils.h",
    "glslang/SPIRV/disassemble.cpp",
    "glslang/SPIRV/disassemble.h",
    "glslang/SPIRV/doc.cpp",
    "glslang/SPIRV/doc.h",
    "glslang/SPIRV/GLSL.ext.AMD.h",
    "glslang/SPIRV/GLSL.ext.EXT.h",
    "glslang/SPIRV/GLSL.ext.KHR.h",
    "glslang/SPIRV/GLSL.ext.NV.h",
    "glslang/SPIRV/GLSL.std.450.h",
    -- Disabled because GLSL is not used.
    -- "glslang/SPIRV/GlslangToSpv.cpp",
    -- "glslang/SPIRV/GlslangToSpv.h",
    "glslang/SPIRV/hex_float.h",
    "glslang/SPIRV/InReadableOrder.cpp",
    "glslang/SPIRV/Logger.cpp",
    "glslang/SPIRV/Logger.h",
    "glslang/SPIRV/NonSemanticDebugPrintf.h",
    "glslang/SPIRV/spirv.hpp",
    "glslang/SPIRV/SpvBuilder.cpp",
    "glslang/SPIRV/SpvBuilder.h",
    "glslang/SPIRV/spvIR.h",
    -- Disabled because of spirv-tools dependency.
    -- "glslang/SPIRV/SpvPostProcess.cpp",
    "glslang/SPIRV/SPVRemapper.cpp",
    "glslang/SPIRV/SPVRemapper.h",
    -- Disabled because of spirv-tools dependency.
    -- "glslang/SPIRV/SpvTools.cpp",
    -- "glslang/SPIRV/SpvTools.h",
  })
