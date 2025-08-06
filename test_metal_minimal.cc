// Minimal test to isolate Metal initialization crash
#include <iostream>
#include "third_party/metal-cpp/Metal/Metal.hpp"
#include "third_party/metal-cpp/Foundation/Foundation.hpp"

extern "C" {
  void* objc_autoreleasePoolPush(void);
  void objc_autoreleasePoolPop(void*);
}

void test_metal_init() {
  std::cout << "Creating Metal device..." << std::endl;
  MTL::Device* device = MTL::CreateSystemDefaultDevice();
  if (!device) {
    std::cout << "Failed to create device" << std::endl;
    return;
  }
  std::cout << "Device created at: " << device << std::endl;
  
  // Create a buffer with a label
  std::cout << "Creating buffer..." << std::endl;
  MTL::Buffer* buffer = device->newBuffer(1024, MTL::ResourceStorageModeShared);
  if (buffer) {
    std::cout << "Buffer created at: " << buffer << std::endl;
    
    // Set label using NS::String
    NS::String* label = NS::String::string("TestBuffer", NS::UTF8StringEncoding);
    std::cout << "Label created at: " << label << std::endl;
    buffer->setLabel(label);
    // Note: label is autoreleased, don't manually release
    
    // Clean up buffer
    std::cout << "Releasing buffer..." << std::endl;
    buffer->release();
  }
  
  // Clean up device
  std::cout << "Releasing device..." << std::endl;
  device->release();
  std::cout << "Done with Metal test" << std::endl;
}

int main() {
  std::cout << "Starting minimal Metal test..." << std::endl;
  
  // Create autorelease pool
  void* pool = objc_autoreleasePoolPush();
  std::cout << "Autorelease pool created" << std::endl;
  
  test_metal_init();
  
  // Drain autorelease pool
  std::cout << "Draining autorelease pool..." << std::endl;
  objc_autoreleasePoolPop(pool);
  std::cout << "Autorelease pool drained successfully" << std::endl;
  
  return 0;
}