#include <iostream>
#include <sys/mman.h>
#include <pthread.h>
#include <libkern/OSCacheControl.h>
#include <stdint.h>

// Simple test to verify MAP_JIT functionality
int main() {
    std::cout << "Testing MAP_JIT functionality on ARM64 macOS...\n";
    
    // Allocate JIT memory
    size_t size = 4096;
    void* jit_mem = mmap(nullptr, size, 
                        PROT_READ | PROT_WRITE | PROT_EXEC,
                        MAP_PRIVATE | MAP_ANON | MAP_JIT, 
                        -1, 0);
    
    if (jit_mem == MAP_FAILED) {
        std::cerr << "Failed to allocate MAP_JIT memory\n";
        return 1;
    }
    
    std::cout << "MAP_JIT memory allocated at: " << jit_mem << "\n";
    
    // ARM64 code for: mov x0, #42; ret
    uint32_t code[] = {
        0xd2800540,  // mov x0, #42
        0xd65f03c0   // ret
    };
    
    // Enable write mode
    pthread_jit_write_protect_np(0);
    std::cout << "Write mode enabled\n";
    
    // Copy code
    memcpy(jit_mem, code, sizeof(code));
    std::cout << "Code copied\n";
    
    // Enable execute mode and flush cache
    pthread_jit_write_protect_np(1);
    sys_icache_invalidate(jit_mem, sizeof(code));
    std::cout << "Execute mode enabled and cache flushed\n";
    
    // Execute the code
    typedef int (*func_t)();
    func_t func = reinterpret_cast<func_t>(jit_mem);
    
    std::cout << "About to execute JIT code...\n";
    int result = func();
    std::cout << "JIT execution successful! Result: " << result << "\n";
    
    // Cleanup
    munmap(jit_mem, size);
    
    return 0;
}
