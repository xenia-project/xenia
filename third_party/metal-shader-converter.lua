group("third_party")
project("metal-shader-converter")
  uuid("b1b2c3d4-e5f6-7890-abcd-ef1234567890")
  kind("None")  -- Header-only project that provides system library links
  language("C++")
  
  filter("system:macosx")
    defines({
      "METAL_SHADER_CONVERTER_AVAILABLE=1",
    })
    -- Use system installation of Metal Shader Converter
    includedirs({
      "/opt/metal-shaderconverter/include",
    })
    -- Link against the Metal Shader Converter dynamic library from system installation
    libdirs({
      "/opt/metal-shaderconverter/lib",
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
    defines({
      "METAL_SHADER_CONVERTER_UNAVAILABLE=1",
    })
