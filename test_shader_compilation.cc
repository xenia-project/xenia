// Simple test to verify shader compilation is working
#include <iostream>
#include <vector>
#include <cstdio>
#include "src/xenia/gpu/metal/metal_shader.h"
#include "src/xenia/base/logging.h"

int main() {
  xe::InitializeLogging("/tmp/test_shader.log");
  
  // Simple vertex shader DXBC bytecode
  uint32_t vs_dwords[] = {
    0x43425844, 0x12345678, 0x87654321, 0xABCDEF00, 0x00000000, 0x00000005,
    // Add minimal DXBC header...
  };
  
  // Create shader
  auto shader = std::make_unique<xe::gpu::metal::MetalShader>(
    xe::gpu::xenos::ShaderType::kVertex,
    0x1234567890ABCDEF,
    vs_dwords,
    sizeof(vs_dwords) / sizeof(uint32_t)
  );
  
  // Try to translate
  auto translation = shader->CreateTranslationInstance(0);
  if (translation) {
    auto* metal_translation = static_cast<xe::gpu::metal::MetalShader::MetalTranslation*>(translation);
    
    // Attempt conversion
    bool success = metal_translation->ConvertToMetal();
    
    std::cout << "Shader conversion " << (success ? "SUCCEEDED" : "FAILED") << std::endl;
    
    if (success && metal_translation->GetMetalFunction()) {
      std::cout << "Metal function created successfully!" << std::endl;
    } else {
      std::cout << "No Metal function created" << std::endl;
    }
    
    delete translation;
  } else {
    std::cout << "Failed to create translation" << std::endl;
  }
  
  return 0;
}