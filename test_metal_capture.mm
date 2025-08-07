// Simple test to verify Metal texture capture and PNG output
#include <iostream>
#include <vector>
#include "third_party/metal-cpp/Metal/Metal.hpp"
#include "third_party/stb/stb_image_write.h"

int main() {
  // Create a simple test texture with a pattern
  MTL::Device* device = MTL::CreateSystemDefaultDevice();
  if (!device) {
    std::cerr << "Failed to create Metal device\n";
    return 1;
  }
  
  // Create a 256x256 BGRA texture with a test pattern
  MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
  desc->setTextureType(MTL::TextureType2D);
  desc->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
  desc->setWidth(256);
  desc->setHeight(256);
  desc->setUsage(MTL::TextureUsageShaderRead | MTL::TextureUsageRenderTarget);
  
  MTL::Texture* texture = device->newTexture(desc);
  desc->release();
  
  // Fill with test pattern
  std::vector<uint8_t> data(256 * 256 * 4);
  for (int y = 0; y < 256; y++) {
    for (int x = 0; x < 256; x++) {
      int idx = (y * 256 + x) * 4;
      data[idx + 0] = x;      // B
      data[idx + 1] = y;      // G  
      data[idx + 2] = 255-x;  // R
      data[idx + 3] = 255;    // A
    }
  }
  
  MTL::Region region(0, 0, 0, 256, 256, 1);
  texture->replaceRegion(region, 0, data.data(), 256 * 4);
  
  // Now read it back
  MTL::CommandQueue* queue = device->newCommandQueue();
  MTL::CommandBuffer* cmd = queue->commandBuffer();
  MTL::BlitCommandEncoder* blit = cmd->blitCommandEncoder();
  
  MTL::Buffer* buffer = device->newBuffer(256 * 256 * 4, MTL::ResourceStorageModeShared);
  blit->copyFromTexture(texture, 0, 0, MTL::Origin(0,0,0), MTL::Size(256,256,1),
                        buffer, 0, 256*4, 256*256*4);
  blit->endEncoding();
  
  cmd->commit();
  cmd->waitUntilCompleted();
  
  // Check if data is black
  uint8_t* readback = (uint8_t*)buffer->contents();
  bool all_black = true;
  for (int i = 0; i < 1000; i++) {
    if (readback[i] != 0) {
      all_black = false;
      break;
    }
  }
  
  if (all_black) {
    std::cout << "ERROR: Texture readback is all black!\n";
  } else {
    std::cout << "SUCCESS: Texture contains data\n";
    std::cout << "First pixel BGRA: [" << (int)readback[0] << "," 
              << (int)readback[1] << "," << (int)readback[2] << "," 
              << (int)readback[3] << "]\n";
  }
  
  // Save as PNG
  stbi_write_png("test_metal_output.png", 256, 256, 4, readback, 256*4);
  std::cout << "Saved test_metal_output.png\n";
  
  // Clean up
  buffer->release();
  texture->release();
  queue->release();
  device->release();
  
  return 0;
}