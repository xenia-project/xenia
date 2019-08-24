/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2019 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <cinttypes>
#include <cstring>
#include <string>
#include <vector>

#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/main.h"
#include "xenia/base/platform.h"
#include "xenia/base/string.h"
#include "xenia/gpu/dxbc_shader_translator.h"
#include "xenia/gpu/glsl_shader_translator.h"
#include "xenia/gpu/shader_translator.h"
#include "xenia/gpu/spirv_shader_translator.h"
#include "xenia/ui/spirv/spirv_disassembler.h"

// For D3DDisassemble:
#if XE_PLATFORM_WIN32
#include "xenia/ui/d3d12/d3d12_api.h"
#endif  // XE_PLATFORM_WIN32

DEFINE_string(shader_input, "", "Input shader binary file path.", "GPU");
DEFINE_string(shader_input_type, "",
              "'vs', 'ps', or unspecified to infer from the given filename.",
              "GPU");
DEFINE_string(shader_output, "", "Output shader file path.", "GPU");
DEFINE_string(shader_output_type, "ucode",
              "Translator to use: [ucode, glsl45, spirv, spirvtext, dxbc].",
              "GPU");
DEFINE_string(shader_output_patch, "",
              "Tessellation patch type in the generated tessellation "
              "evaluation (domain) shader, or unspecified to produce a vertex "
              "shader: [line, triangle, quad].",
              "GPU");
DEFINE_bool(shader_output_dxbc_rov, false,
            "Output ROV-based output-merger code in DXBC pixel shaders.",
            "GPU");

namespace xe {
namespace gpu {

int shader_compiler_main(const std::vector<std::wstring>& args) {
  ShaderType shader_type;
  if (!cvars::shader_input_type.empty()) {
    if (cvars::shader_input_type == "vs") {
      shader_type = ShaderType::kVertex;
    } else if (cvars::shader_input_type == "ps") {
      shader_type = ShaderType::kPixel;
    } else {
      XELOGE("Invalid --shader_input_type; must be 'vs' or 'ps'.");
      return 1;
    }
  } else {
    auto last_dot = cvars::shader_input.find_last_of('.');
    bool valid_type = false;
    if (last_dot != std::string::npos) {
      if (cvars::shader_input.substr(last_dot) == ".vs") {
        shader_type = ShaderType::kVertex;
        valid_type = true;
      } else if (cvars::shader_input.substr(last_dot) == ".ps") {
        shader_type = ShaderType::kPixel;
        valid_type = true;
      }
    }
    if (!valid_type) {
      XELOGE(
          "File type not recognized (use .vs, .ps or "
          "--shader_input_type=vs|ps).");
      return 1;
    }
  }

  auto input_file = fopen(cvars::shader_input.c_str(), "rb");
  if (!input_file) {
    XELOGE("Unable to open input file: %s", cvars::shader_input.c_str());
    return 1;
  }
  fseek(input_file, 0, SEEK_END);
  size_t input_file_size = ftell(input_file);
  fseek(input_file, 0, SEEK_SET);
  std::vector<uint32_t> ucode_dwords(input_file_size / 4);
  fread(ucode_dwords.data(), 4, ucode_dwords.size(), input_file);
  fclose(input_file);

  XELOGI("Opened %s as a %s shader, %" PRId64 " words (%" PRId64 " bytes).",
         cvars::shader_input.c_str(),
         shader_type == ShaderType::kVertex ? "vertex" : "pixel",
         ucode_dwords.size(), ucode_dwords.size() * 4);

  // TODO(benvanik): hash? need to return the data to big-endian format first.
  uint64_t ucode_data_hash = 0;
  auto shader = std::make_unique<Shader>(
      shader_type, ucode_data_hash, ucode_dwords.data(), ucode_dwords.size());

  std::unique_ptr<ShaderTranslator> translator;
  if (cvars::shader_output_type == "spirv" ||
      cvars::shader_output_type == "spirvtext") {
    translator = std::make_unique<SpirvShaderTranslator>();
  } else if (cvars::shader_output_type == "glsl45") {
    translator = std::make_unique<GlslShaderTranslator>(
        GlslShaderTranslator::Dialect::kGL45);
  } else if (cvars::shader_output_type == "dxbc") {
    translator = std::make_unique<DxbcShaderTranslator>(
        0, cvars::shader_output_dxbc_rov);
  } else {
    translator = std::make_unique<UcodeShaderTranslator>();
  }

  PrimitiveType patch_primitive_type = PrimitiveType::kNone;
  if (shader_type == ShaderType::kVertex) {
    if (cvars::shader_output_patch == "line") {
      patch_primitive_type = PrimitiveType::kLinePatch;
    } else if (cvars::shader_output_patch == "triangle") {
      patch_primitive_type = PrimitiveType::kTrianglePatch;
    } else if (cvars::shader_output_patch == "quad") {
      patch_primitive_type = PrimitiveType::kQuadPatch;
    }
  }

  translator->Translate(shader.get(), patch_primitive_type);

  const void* source_data = shader->translated_binary().data();
  size_t source_data_size = shader->translated_binary().size();

  std::unique_ptr<xe::ui::spirv::SpirvDisassembler::Result> spirv_disasm_result;
  if (cvars::shader_output_type == "spirvtext") {
    // Disassemble SPIRV.
    spirv_disasm_result = xe::ui::spirv::SpirvDisassembler().Disassemble(
        reinterpret_cast<const uint32_t*>(source_data), source_data_size / 4);
    source_data = spirv_disasm_result->text();
    source_data_size = std::strlen(spirv_disasm_result->text()) + 1;
  }
#if XE_PLATFORM_WIN32
  ID3DBlob* dxbc_disasm_blob = nullptr;
  if (cvars::shader_output_type == "dxbc") {
    HMODULE d3d_compiler = LoadLibrary(L"D3DCompiler_47.dll");
    if (d3d_compiler != nullptr) {
      pD3DDisassemble d3d_disassemble =
          pD3DDisassemble(GetProcAddress(d3d_compiler, "D3DDisassemble"));
      if (d3d_disassemble != nullptr) {
        // Disassemble DXBC.
        if (SUCCEEDED(d3d_disassemble(source_data, source_data_size,
                                      D3D_DISASM_ENABLE_INSTRUCTION_NUMBERING |
                                          D3D_DISASM_ENABLE_INSTRUCTION_OFFSET,
                                      nullptr, &dxbc_disasm_blob))) {
          source_data = dxbc_disasm_blob->GetBufferPointer();
          source_data_size = dxbc_disasm_blob->GetBufferSize();
        }
      }
      FreeLibrary(d3d_compiler);
    }
  }
#endif  // XE_PLATFORM_WIN32

  if (!cvars::shader_output.empty()) {
    auto output_file = fopen(cvars::shader_output.c_str(), "wb");
    fwrite(source_data, 1, source_data_size, output_file);
    fclose(output_file);
  }

#if XE_PLATFORM_WIN32
  if (dxbc_disasm_blob != nullptr) {
    dxbc_disasm_blob->Release();
  }
#endif  // XE_PLATFORM_WIN32

  return 0;
}

}  // namespace gpu
}  // namespace xe

DEFINE_ENTRY_POINT(L"xenia-gpu-shader-compiler", xe::gpu::shader_compiler_main,
                   "shader.bin", "shader_input");
