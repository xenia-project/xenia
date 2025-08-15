/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/metal/metal_shader.h"

#include <cstring>
#include <utility>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/gpu/dxbc_shader.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/metal/dxbc_to_dxil_converter.h"
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

bool MetalShader::MetalTranslation::TranslateToMetalIR(
    DxbcToDxilConverter& converter, IRCompiler* compiler,
    IRRootSignature* root_signature) {
        std::string error_message;
        if (!converter.Convert(translated_binary(), dxil_data_, &error_message)) {
            XELOGE("Unable to Convert DXBC to DXIL: {}", error_message);
        }
        XELOGD("Converted DXBC -> DXIL Successfully");
        IRObject* dxil = IRObjectCreateFromDXIL(dxil_data_.data(), sizeof(dxil_data_) * dxil_data_.size(), IRBytecodeOwnershipNone);

        // Compile DXIL to Metal IR;
        IRError* error = nullptr;
        IRObject* metal_ir = IRCompilerAllocCompileAndLink(compiler, nullptr, dxil, &error);

        if (!metal_ir) {
            uint32_t error_code = IRErrorGetCode(error);
            switch (error_code) {
                case IRErrorCodeCompilationError:
                    XELOGE("Metal shader conversion failed: compilation error");
                    break;
                case IRErrorCodeFailedToSynthesizeIndirectIntersectionFunction:
                    XELOGE("Metal shader conversion failed: Failed to Synthesize Indrect Intersection Function");
                    break;
                case IRErrorCodeUnrecognizedDXILHeader:
                    XELOGE("Metal shader conversion failed: Unrecognized DXIL Header");
                    break;
                // TODO: Handle other error codes
                case IRErrorCodeUnsupportedInstruction:
                    XELOGE("Metal shader conversion failed: Unsupported Instruction");
                default:
                    XELOGE("Metal shader conversion failed with error code: {}", error_code);
            }
            IRErrorDestroy ( error );
            return false;
        }
        IRShaderStage compiled_stage = IRObjectGetMetalIRShaderStage(metal_ir);
        IRMetalLibBinary* metal_lib = IRMetalLibBinaryCreate();
        if (!IRObjectGetMetalLibBinary(metal_ir, compiled_stage, metal_lib)) {
            XELOGE("Failed to extract Metal library for stage {}", compiled_stage);
            IRMetalLibBinaryDestroy(metal_lib);
            return false;
        }
        return true;
    }

Shader::Translation* MetalShader::CreateTranslationInstance(
    uint64_t modification) {
  return new MetalTranslation(*this, modification);
}

}  // namespace metal
}  // namespace gpu
}  // namespace xe