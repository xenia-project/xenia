group("third_party")
project("dxbc2dxil")
  kind "Utility"
  
  filter "system:macosx"
    local dxilconv_dir = "third_party/DirectXShaderCompiler"
    -- Just keep the files for visibility
    files {
      dxilconv_dir .. "/build_dxilconv_macos.sh",
      dxilconv_dir .. "/projects/dxilconv/**.cpp",
      dxilconv_dir .. "/projects/dxilconv/**.h"
    }
  filter {}