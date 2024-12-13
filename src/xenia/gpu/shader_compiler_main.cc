/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <cinttypes>
#include <cstring>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "third_party/glslang/SPIRV/disassemble.h"
#include "xenia/base/assert.h"
#include "xenia/base/console_app_main.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform.h"
#include "xenia/base/string.h"
#include "xenia/base/string_buffer.h"
#include "xenia/gpu/dxbc_shader_translator.h"
#include "xenia/gpu/shader_translator.h"
#include "xenia/gpu/spirv_shader_translator.h"
#include "xenia/gpu/xenos.h"
#include "xenia/ui/vulkan/spirv_tools_context.h"

// For D3DDisassemble:
#if XE_PLATFORM_WIN32
#include "xenia/ui/d3d12/d3d12_api.h"
#endif  // XE_PLATFORM_WIN32

DEFINE_path(shader_input, "", "Input shader binary file path.", "GPU");
DEFINE_string(shader_input_type, "",
              "'vs', 'ps', or unspecified to infer from the given filename.",
              "GPU");
DEFINE_bool(
    shader_input_little_endian, false,
    "Whether the input shader binary is little-endian (from an Arm device with "
    "the Qualcomm Adreno 200, for instance).",
    "GPU");
DEFINE_path(shader_output, "", "Output shader file path.", "GPU");
DEFINE_string(shader_output_type, "ucode",
              "Translator to use: [ucode, spirv, spirvtext, dxbc, dxbctext].",
              "GPU");
DEFINE_string(
    vertex_shader_output_type, "",
    "Type of the host interface to produce the vertex or domain shader for: "
    "[vertex or unspecified, linedomaincp, linedomainpatch, triangledomaincp, "
    "triangledomainpatch, quaddomaincp, quaddomainpatch].",
    "GPU");
DEFINE_bool(shader_output_bindless_resources, false,
            "Output host shader with bindless resources used.", "GPU");
DEFINE_bool(
    shader_output_pixel_shader_interlock, false,
    "Output host shader with a render backend implementation based on pixel "
    "shader interlock.",
    "GPU");

namespace xe {
namespace gpu {

int shader_compiler_main(const std::vector<std::string>& args) {
  xenos::ShaderType shader_type;
  if (!cvars::shader_input_type.empty()) {
    if (cvars::shader_input_type == "vs") {
      shader_type = xenos::ShaderType::kVertex;
    } else if (cvars::shader_input_type == "ps") {
      shader_type = xenos::ShaderType::kPixel;
    } else {
      XELOGE("Invalid --shader_input_type; must be 'vs' or 'ps'.");
      return 1;
    }
  } else {
    bool valid_type = false;
    if (cvars::shader_input.has_extension()) {
      auto extension = cvars::shader_input.extension();
      if (extension == ".vs") {
        shader_type = xenos::ShaderType::kVertex;
        valid_type = true;
      } else if (extension == ".ps") {
        shader_type = xenos::ShaderType::kPixel;
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

  auto input_file = filesystem::OpenFile(cvars::shader_input, "rb");
  if (!input_file) {
    XELOGE("Unable to open input file: {}", cvars::shader_input);
    return 1;
  }
  size_t input_file_size = std::filesystem::file_size(cvars::shader_input);
  std::vector<uint32_t> ucode_dwords(input_file_size / 4);
  fread(ucode_dwords.data(), 4, ucode_dwords.size(), input_file);
  fclose(input_file);

  XELOGI("Opened {} as a {} shader, {} words ({} bytes).", cvars::shader_input,
         shader_type == xenos::ShaderType::kVertex ? "vertex" : "pixel",
         ucode_dwords.size(), ucode_dwords.size() * 4);

  // TODO(benvanik): hash? need to return the data to big-endian format first.
  uint64_t ucode_data_hash = 0;
  auto shader = std::make_unique<Shader>(
      shader_type, ucode_data_hash, ucode_dwords.data(), ucode_dwords.size(),
      cvars::shader_input_little_endian ? std::endian::little
                                        : std::endian::big);

  StringBuffer ucode_disasm_buffer;
  shader->AnalyzeUcode(ucode_disasm_buffer);

  std::unique_ptr<ShaderTranslator> translator;
  SpirvShaderTranslator::Features spirv_features(true);
  if (cvars::shader_output_type == "spirv" ||
      cvars::shader_output_type == "spirvtext") {
    translator = std::make_unique<SpirvShaderTranslator>(
        spirv_features, true, true,
        cvars::shader_output_pixel_shader_interlock);
  } else if (cvars::shader_output_type == "dxbc" ||
             cvars::shader_output_type == "dxbctext") {
    translator = std::make_unique<DxbcShaderTranslator>(
        ui::GraphicsProvider::GpuVendorID(0),
        cvars::shader_output_bindless_resources,
        cvars::shader_output_pixel_shader_interlock);
  } else {
    // Just output microcode disassembly generated during microcode information
    // gathering.
    if (!cvars::shader_output.empty()) {
      auto output_file = filesystem::OpenFile(cvars::shader_output, "wb");
      fwrite(shader->ucode_disassembly().c_str(), 1,
             shader->ucode_disassembly().length(), output_file);
      fclose(output_file);
    }
    return 0;
  }

  Shader::HostVertexShaderType host_vertex_shader_type =
      Shader::HostVertexShaderType::kVertex;
  if (shader_type == xenos::ShaderType::kVertex) {
    if (cvars::vertex_shader_output_type == "linedomaincp") {
      host_vertex_shader_type =
          Shader::HostVertexShaderType::kLineDomainCPIndexed;
    } else if (cvars::vertex_shader_output_type == "linedomainpatch") {
      host_vertex_shader_type =
          Shader::HostVertexShaderType::kLineDomainPatchIndexed;
    } else if (cvars::vertex_shader_output_type == "triangledomaincp") {
      host_vertex_shader_type =
          Shader::HostVertexShaderType::kTriangleDomainCPIndexed;
    } else if (cvars::vertex_shader_output_type == "triangledomainpatch") {
      host_vertex_shader_type =
          Shader::HostVertexShaderType::kTriangleDomainPatchIndexed;
    } else if (cvars::vertex_shader_output_type == "quaddomaincp") {
      host_vertex_shader_type =
          Shader::HostVertexShaderType::kQuadDomainCPIndexed;
    } else if (cvars::vertex_shader_output_type == "quaddomainpatch") {
      host_vertex_shader_type =
          Shader::HostVertexShaderType::kQuadDomainPatchIndexed;
    }
  }
  uint64_t modification;
  switch (shader_type) {
    case xenos::ShaderType::kVertex:
      modification = translator->GetDefaultVertexShaderModification(
          xenos::kMaxShaderTempRegisters, host_vertex_shader_type);
      break;
    case xenos::ShaderType::kPixel:
      modification = translator->GetDefaultPixelShaderModification(
          xenos::kMaxShaderTempRegisters);
      break;
    default:
      assert_unhandled_case(shader_type);
      return 1;
  }

  Shader::Translation* translation =
      shader->GetOrCreateTranslation(modification);
  translator->TranslateAnalyzedShader(*translation);

  const void* source_data = translation->translated_binary().data();
  size_t source_data_size = translation->translated_binary().size();

  std::string spirv_disasm;
  if (cvars::shader_output_type == "spirvtext") {
    std::ostringstream spirv_disasm_stream;
    std::vector<unsigned int> spirv_source;
    spirv_source.reserve(source_data_size / sizeof(unsigned int));
    spirv_source.insert(spirv_source.cend(),
                        reinterpret_cast<const unsigned int*>(source_data),
                        reinterpret_cast<const unsigned int*>(source_data) +
                            source_data_size / sizeof(unsigned int));
    spv::Disassemble(spirv_disasm_stream, spirv_source);
    spirv_disasm = std::move(spirv_disasm_stream.str());
    ui::vulkan::SpirvToolsContext spirv_tools_context;
    if (spirv_tools_context.Initialize(spirv_features.spirv_version)) {
      std::string spirv_validation_error;
      spirv_tools_context.Validate(
          reinterpret_cast<const uint32_t*>(spirv_source.data()),
          spirv_source.size(), &spirv_validation_error);
      if (!spirv_validation_error.empty()) {
        spirv_disasm.append(1, '\n');
        spirv_disasm.append(spirv_validation_error);
      }
    }
    source_data = spirv_disasm.c_str();
    source_data_size = spirv_disasm.size();
  }
#if XE_PLATFORM_WIN32
  ID3DBlob* dxbc_disasm_blob = nullptr;
  if (cvars::shader_output_type == "dxbctext") {
    HMODULE d3d_compiler = LoadLibraryW(L"D3DCompiler_47.dll");
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
          // Stop at the null terminator.
          for (size_t i = 0; i < source_data_size; ++i) {
            if (reinterpret_cast<const char*>(source_data)[i] == '\0') {
              source_data_size = i;
              break;
            }
          }
        }
      }
      FreeLibrary(d3d_compiler);
    }
  }
#endif  // XE_PLATFORM_WIN32

  if (!cvars::shader_output.empty()) {
    auto output_file = filesystem::OpenFile(cvars::shader_output, "wb");
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

XE_DEFINE_CONSOLE_APP("xenia-gpu-shader-compiler",
                      xe::gpu::shader_compiler_main, "shader.bin",
                      "shader_input");
