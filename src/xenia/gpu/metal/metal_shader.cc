/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/metal/metal_shader.h"

#include <cstdlib>
#include <cstring>

#include "xenia/base/assert.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/gpu/dxbc_shader.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/metal/dxbc_to_dxil_converter.h"
#include "xenia/gpu/metal/metal_shader_converter.h"
#include "xenia/ui/metal/metal_api.h"

namespace xe {
namespace gpu {
namespace metal {

MetalShader::MetalShader(xenos::ShaderType shader_type,
                         uint64_t ucode_data_hash, const uint32_t* ucode_dwords,
                         size_t ucode_dword_count,
                         std::endian ucode_source_endian)
    : DxbcShader(shader_type, ucode_data_hash, ucode_dwords, ucode_dword_count,
                 ucode_source_endian) {}

MetalShader::MetalTranslation::~MetalTranslation() {
  if (metal_function_) {
    metal_function_->release();
    metal_function_ = nullptr;
  }
  if (metal_library_) {
    metal_library_->release();
    metal_library_ = nullptr;
  }
}

bool MetalShader::MetalTranslation::TranslateToMetal(
    MTL::Device* device, DxbcToDxilConverter& dxbc_converter,
    MetalShaderConverter& metal_converter) {
  if (!device) {
    XELOGE("MetalShader: No Metal device provided");
    return false;
  }

  // Get the translated DXBC bytecode from the base class
  const std::vector<uint8_t>& dxbc_data = translated_binary();
  if (dxbc_data.empty()) {
    XELOGE("MetalShader: No translated DXBC data available");
    return false;
  }

  // Step 1: Convert DXBC to DXIL using native dxbc2dxil
  std::string dxbc_error;
  if (!dxbc_converter.Convert(dxbc_data, dxil_data_, &dxbc_error)) {
    XELOGE("MetalShader: DXBC to DXIL conversion failed: {}", dxbc_error);
    return false;
  }
  XELOGD("MetalShader: Converted {} bytes DXBC to {} bytes DXIL",
         dxbc_data.size(), dxil_data_.size());

  // Step 2: Convert DXIL to MetalLib using Metal Shader Converter
  MetalShaderConversionResult msc_result;
  if (!metal_converter.Convert(shader().type(), dxil_data_, msc_result)) {
    XELOGE("MetalShader: DXIL to Metal conversion failed: {}",
           msc_result.error_message);
    return false;
  }
  function_name_ = msc_result.function_name;
  metallib_data_ = std::move(msc_result.metallib_data);
  XELOGD("MetalShader: Converted {} bytes DXIL to {} bytes MetalLib",
         dxil_data_.size(), metallib_data_.size());

  // Debug: Dump shader artifacts (DXBC, DXIL, MetalLib) to files when enabled.
  static int shader_dump_counter = 0;
  if (!cvars::dump_shaders.empty()) {
    const char* dump_dir = cvars::dump_shaders.c_str();

    char filename[512];
    const char* type_str =
        (shader().type() == xenos::ShaderType::kVertex) ? "vs" : "ps";
    int counter = shader_dump_counter++;

    // Dump DXBC (translated binary from DXBC translator)
    const auto& dxbc_data = translated_binary();
    if (!dxbc_data.empty()) {
      snprintf(filename, sizeof(filename), "%s/shader_%d_%s.dxbc", dump_dir,
               counter, type_str);
      FILE* f = fopen(filename, "wb");
      if (f) {
        fwrite(dxbc_data.data(), 1, dxbc_data.size(), f);
        fclose(f);
        XELOGI("DEBUG: Dumped DXBC ({} bytes) to {}", dxbc_data.size(),
               filename);
      }
    }

    // Dump DXIL
    if (!dxil_data_.empty()) {
      snprintf(filename, sizeof(filename), "%s/shader_%d_%s.dxil", dump_dir,
               counter, type_str);
      FILE* f = fopen(filename, "wb");
      if (f) {
        fwrite(dxil_data_.data(), 1, dxil_data_.size(), f);
        fclose(f);
        XELOGI("DEBUG: Dumped DXIL ({} bytes) to {}", dxil_data_.size(),
               filename);
      }
    }

    // Dump MetalLib
    if (!metallib_data_.empty()) {
      snprintf(filename, sizeof(filename), "%s/shader_%d_%s.metallib", dump_dir,
               counter, type_str);
      FILE* f = fopen(filename, "wb");
      if (f) {
        fwrite(metallib_data_.data(), 1, metallib_data_.size(), f);
        fclose(f);
        XELOGI("DEBUG: Dumped MetalLib ({} bytes) to {}", metallib_data_.size(),
               filename);
      }
    }
  }

  // Step 3: Create Metal library from the metallib data
  NS::Error* error = nullptr;
  dispatch_data_t data =
      dispatch_data_create(metallib_data_.data(), metallib_data_.size(),
                           nullptr, DISPATCH_DATA_DESTRUCTOR_DEFAULT);

  metal_library_ = device->newLibrary(data, &error);
  dispatch_release(data);

  if (!metal_library_) {
    if (error) {
      XELOGE("MetalShader: Failed to create Metal library: {}",
             error->localizedDescription()->utf8String());
    } else {
      XELOGE("MetalShader: Failed to create Metal library (unknown error)");
    }
    return false;
  }

  // Step 4: Get the main function from the library
  // MSC generates functions with specific names based on shader type
  NS::String* function_name = NS::String::string(
      msc_result.function_name.c_str(), NS::UTF8StringEncoding);

  XELOGI("GPU DEBUG: Looking for shader function '{}' in metallib ({} bytes)",
         msc_result.function_name, metallib_data_.size());

  metal_function_ = metal_library_->newFunction(function_name);

  if (!metal_function_) {
    // Try alternative function names
    const char* alt_names[] = {"main0", "main", "vertexMain", "fragmentMain"};
    for (const char* alt_name : alt_names) {
      NS::String* alt_func_name =
          NS::String::string(alt_name, NS::UTF8StringEncoding);
      metal_function_ = metal_library_->newFunction(alt_func_name);
      if (metal_function_) {
        XELOGD("MetalShader: Found function with alternative name: {}",
               alt_name);
        break;
      }
    }
  }

  if (!metal_function_) {
    // List available functions for debugging
    NS::Array* function_names = metal_library_->functionNames();
    XELOGE("MetalShader: Could not find shader function. Available functions:");
    for (NS::UInteger i = 0; i < function_names->count(); i++) {
      NS::String* name = static_cast<NS::String*>(function_names->object(i));
      XELOGE("  - {}", name->utf8String());
    }
    return false;
  }

  XELOGI("MetalShader: Successfully created Metal shader function");
  return true;
}

Shader::Translation* MetalShader::CreateTranslationInstance(
    uint64_t modification) {
  return new MetalTranslation(*this, modification);
}

}  // namespace metal
}  // namespace gpu
}  // namespace xe
