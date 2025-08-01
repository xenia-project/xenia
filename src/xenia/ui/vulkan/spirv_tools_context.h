/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_VULKAN_SPIRV_TOOLS_CONTEXT_H_
#define XENIA_UI_VULKAN_SPIRV_TOOLS_CONTEXT_H_

#include <cstdint>
#include <string>

#include "third_party/SPIRV-Tools/include/spirv-tools/libspirv.h"
#include "xenia/base/platform.h"

#if XE_PLATFORM_LINUX || XE_PLATFORM_MAC
#include <dlfcn.h>
#elif XE_PLATFORM_WIN32
#include "xenia/base/platform_win.h"
#endif

namespace xe {
namespace ui {
namespace vulkan {

class SpirvToolsContext {
 public:
  SpirvToolsContext() {}
  SpirvToolsContext(const SpirvToolsContext& context) = delete;
  SpirvToolsContext& operator=(const SpirvToolsContext& context) = delete;
  ~SpirvToolsContext() { Shutdown(); }
  bool Initialize(unsigned int spirv_version);
  void Shutdown();

  spv_result_t Validate(const uint32_t* words, size_t num_words,
                        std::string* error) const;

 private:
#if XE_PLATFORM_LINUX || XE_PLATFORM_MAC
  void* library_ = nullptr;
#elif XE_PLATFORM_WIN32
  HMODULE library_ = nullptr;
#endif

  template <typename FunctionPointer>
  bool LoadLibraryFunction(FunctionPointer& function, const char* name) {
#if XE_PLATFORM_LINUX || XE_PLATFORM_MAC
    function = reinterpret_cast<FunctionPointer>(dlsym(library_, name));
#elif XE_PLATFORM_WIN32
    function =
        reinterpret_cast<FunctionPointer>(GetProcAddress(library_, name));
#else
#error No SPIRV-Tools LoadLibraryFunction provided for the target platform.
#endif
    return function != nullptr;
  }
  decltype(&spvContextCreate) fn_spvContextCreate_ = nullptr;
  decltype(&spvContextDestroy) fn_spvContextDestroy_ = nullptr;
  decltype(&spvValidateBinary) fn_spvValidateBinary_ = nullptr;
  decltype(&spvDiagnosticDestroy) fn_spvDiagnosticDestroy_ = nullptr;

  spv_context context_ = nullptr;
};

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VULKAN_SPIRV_TOOLS_CONTEXT_H_
