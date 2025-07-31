# Xenia macOS ARM64 JIT Hardened Runtime Support

## Overview
This document outlines the changes made to support JIT compilation with macOS Hardened Runtime capability on Apple Silicon Macs.

## Key Changes Made

### 1. Entitlements File (`xenia.entitlements`)
Created a comprehensive entitlements file with:
- `com.apple.security.cs.allow-jit` - Allows JIT compilation with MAP_JIT flag
- `com.apple.security.cs.jit-write-allowlist` - Enables JIT callback allowlists
- `com.apple.security.cs.jit-write-allowlist-freeze-late` - Allows dynamic library callbacks
- `com.apple.security.cs.disable-library-validation` - May be needed for emulator plugins

### 2. JIT Write Callback Implementation (`a64_code_cache_mac.cc`)
#### Added Hardened Runtime Compatible JIT Writing:
- `JITWriteContext` structure for safe parameter passing
- `jit_write_callback()` function with input validation
- `PTHREAD_JIT_WRITE_ALLOW_CALLBACKS_NP()` macro to register callback
- Early callback freezing with `pthread_jit_write_freeze_callbacks_np()`

#### Updated Memory Writing Strategy:
- Primary: Use `pthread_jit_write_with_callback_np()` for Hardened Runtime
- Fallback: Use `pthread_jit_write_protect_np()` for non-Hardened Runtime
- Automatic memory protection handling with callbacks

### 3. Enhanced Memory State Tracking (`memory_mac.cc`)
#### Added Detailed Logging:
- MAP_JIT flag usage tracking
- Memory protection state logging  
- Allocation success/failure with detailed error info
- Memory verification utilities

#### Added Utility Functions:
- `VerifyJITMemoryState()` for debugging memory accessibility

### 4. Build Script (`build_with_jit.sh`)
Created automated build script that:
- Generates Xcode projects with premake5
- Builds the project with Release configuration
- Applies JIT entitlements using codesign
- Verifies applied entitlements

## How Hardened Runtime JIT Works

### Traditional Approach (Non-Hardened Runtime):
```cpp
pthread_jit_write_protect_np(0);  // Make writable
memcpy(dest, src, size);          // Copy code
pthread_jit_write_protect_np(1);  // Make executable
```

### Hardened Runtime Approach:
```cpp
// 1. Define callback allowlist (compile time)
PTHREAD_JIT_WRITE_ALLOW_CALLBACKS_NP(jit_write_callback);

// 2. Freeze callbacks early (runtime)
pthread_jit_write_freeze_callbacks_np();

// 3. Write using callback (automatically handles memory protection)
pthread_jit_write_with_callback_np(jit_write_callback, &context);
```

## Memory Protection States

### With MAP_JIT Flag:
- **Initial**: RWX (Read/Write/Execute) but W^X enforced per-thread
- **During Callback**: RW for current thread only
- **After Callback**: RX for all threads

### Memory Transition Tracking:
1. `AllocFixed()` with `PageAccess::kExecuteReadWrite` → MAP_JIT allocation
2. `CopyMachineCode()` → JIT callback or pthread_jit_write_protect_np(0)
3. `PlaceCode()` → Instruction cache flush + pthread_jit_write_protect_np(1) (if needed)

## Debugging Features Added

### Comprehensive Logging:
- JIT callback success/failure
- Memory allocation with protection details
- MAP_JIT flag usage tracking
- Memory state transitions
- Code verification (first 16 bytes dump)

### Error Handling:
- Graceful fallback from callback to traditional approach
- Input validation in JIT callbacks
- Size bounds checking (16MB max per write)
- Memory accessibility verification

## Usage Instructions

### Building with JIT Support:
```bash
./build_with_jit.sh
```

### Manual Entitlement Application:
```bash
codesign --force --options runtime --entitlements xenia.entitlements --sign - /path/to/binary
```

### Verifying Entitlements:
```bash
codesign -d --entitlements :- /path/to/binary
```

## Troubleshooting

### Common Issues:
1. **"Operation not permitted" during mmap**: Missing `com.apple.security.cs.allow-jit` entitlement
2. **Callback crashes**: Callback not in allowlist or callbacks not frozen
3. **Memory protection errors**: W^X violation - ensure proper callback usage

### Debug Steps:
1. Check entitlements are applied: `codesign -d --entitlements :- binary`
2. Look for JIT callback logs in output
3. Verify MAP_JIT allocation logs
4. Check memory protection state transitions

## Next Steps

1. **Test the build**: Run `./build_with_jit.sh` to build with entitlements
2. **Verify JIT functionality**: Check logs for successful callback usage
3. **Debug execution issues**: Use memory state tracking to identify problems
4. **Performance testing**: Compare callback vs traditional approach performance

## References
- [Apple JIT Porting Guide](https://developer.apple.com/documentation/apple-silicon/porting-just-in-time-compilers-to-apple-silicon/)
- [Hardened Runtime Documentation](https://developer.apple.com/documentation/xcode/configuring-the-hardened-runtime/)
- [JIT Entitlements Reference](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.security.cs.allow-jit/)
