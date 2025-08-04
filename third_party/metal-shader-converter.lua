group("third_party")
project("metal-shader-converter")
  uuid("b1b2c3d4-e5f6-7890-abcd-ef1234567890")
  kind("StaticLib")
  language("C++")
  
  filter("system:macosx")
    defines({
      "_LIB",
    })
    includedirs({
      "metal-shader-converter/include",
    })
    files({
      "metal-shader-converter/include/metal_irconverter.h",
      "metal-shader-converter/include/metal_irconverter_runtime.h",
    })
    -- Link against the Metal Shader Converter dynamic library
    libdirs({
      "metal-shader-converter/lib",
    })
    links({
      "metalirconverter",
    })
    -- Add Metal framework dependencies
    links({
      "Metal.framework",
      "MetalKit.framework",
    })
  
  filter("not system:macosx")
    -- Metal Shader Converter is macOS only
    removefiles("**")
    defines({
      "METAL_SHADER_CONVERTER_UNAVAILABLE=1",
    })
