/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <gflags/gflags.h>

#include <cinttypes>
#include <string>
#include <vector>

#include "xenia/base/logging.h"
#include "xenia/base/main.h"
#include "xenia/base/string.h"
#include "xenia/gpu/shader_translator.h"

DEFINE_string(input_shader, "", "Input shader binary file path.");
DEFINE_string(
    shader_type, "",
    "'vert', 'frag', or unspecified to infer from the given filename.");

namespace xe {
namespace gpu {

int shader_compiler_main(const std::vector<std::wstring>& args) {
  bool is_vertex_shader = false;
  if (!FLAGS_shader_type.empty()) {
    if (FLAGS_shader_type == "vs") {
      is_vertex_shader = false;
    }
  } else {
    auto last_dot = FLAGS_input_shader.find_last_of('.');
    bool valid_type = false;
    if (last_dot != std::string::npos) {
      if (FLAGS_input_shader.substr(last_dot) == ".vert") {
        is_vertex_shader = true;
        valid_type = true;
      } else if (FLAGS_input_shader.substr(last_dot) == ".frag") {
        valid_type = true;
      }
    }
    if (!valid_type) {
      XELOGE(
          "File type not recognized (use .vert, .frag or "
          "--shader_type=vert|frag).");
      return 1;
    }
  }

  auto input_file = fopen(FLAGS_input_shader.c_str(), "r");
  if (!input_file) {
    XELOGE("Unable to open input file: %s", FLAGS_input_shader.c_str());
    return 1;
  }
  fseek(input_file, 0, SEEK_END);
  size_t input_file_size = ftell(input_file);
  fseek(input_file, 0, SEEK_SET);
  std::vector<uint32_t> ucode_words(input_file_size / 4);
  fread(ucode_words.data(), 4, ucode_words.size(), input_file);
  fclose(input_file);

  XELOGI("Opened %s as a %s shader, %" PRId64 " words (%" PRId64 " bytes).",
         FLAGS_input_shader.c_str(), is_vertex_shader ? "vertex" : "fragment",
         ucode_words.size(), ucode_words.size() * 4);

  ShaderTranslator translator;

  return 0;
}

}  // namespace gpu
}  // namespace xe

DEFINE_ENTRY_POINT(L"xenia-gpu-shader-compiler",
                   L"xenia-gpu-shader-compiler shader.bin",
                   xe::gpu::shader_compiler_main);
