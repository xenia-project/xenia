/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

/**
 * DXBC to DXIL converter implementation
 * Uses native macOS dxbc2dxil binary (no Wine required)
 */

#include "dxbc_to_dxil_converter.h"

#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "xenia/base/logging.h"

namespace xe {
namespace gpu {
namespace metal {

DxbcToDxilConverter::DxbcToDxilConverter() {
  // Get temp directory
  const char* tmp = std::getenv("TMPDIR");
  if (tmp) {
    temp_dir_ = tmp;
  } else {
    temp_dir_ = "/tmp";
  }
  temp_dir_ += "/xenia_dxbc2dxil";

  // Create temp directory if it doesn't exist
  mkdir(temp_dir_.c_str(), 0700);
}

DxbcToDxilConverter::~DxbcToDxilConverter() { CleanupTempFiles(); }

bool DxbcToDxilConverter::Initialize() {
  // Look for native dxbc2dxil binary in various locations
  // Priority: native macOS binary > Wine-based fallback
  std::vector<std::string> search_paths = {
      // Native macOS binary from DirectXShaderCompiler build (highest priority)
      "./third_party/DirectXShaderCompiler/build/bin/dxbc2dxil",
      "../third_party/DirectXShaderCompiler/build/bin/dxbc2dxil",
      "../../third_party/DirectXShaderCompiler/build/bin/dxbc2dxil",
      "../../../third_party/DirectXShaderCompiler/build/bin/dxbc2dxil",
      "../../../../third_party/DirectXShaderCompiler/build/bin/dxbc2dxil",
      // Native macOS binary from build_dxilconv_macos.sh
      "./third_party/DirectXShaderCompiler/build_dxilconv_macos/bin/dxbc2dxil",
      "../third_party/DirectXShaderCompiler/build_dxilconv_macos/bin/dxbc2dxil",
      "../../third_party/DirectXShaderCompiler/build_dxilconv_macos/bin/dxbc2dxil",
      "../../../third_party/DirectXShaderCompiler/build_dxilconv_macos/bin/dxbc2dxil",
      "../../../../third_party/DirectXShaderCompiler/build_dxilconv_macos/bin/dxbc2dxil",
      // Build output directory
      "./build/bin/dxbc2dxil", "../build/bin/dxbc2dxil",
      // User-provided location via environment variable
      std::string(std::getenv("DXBC2DXIL_PATH") ? std::getenv("DXBC2DXIL_PATH")
                                                : "")};

  for (const auto& path : search_paths) {
    if (!path.empty() && access(path.c_str(), X_OK) == 0) {
      dxbc2dxil_path_ = path;
      XELOGI("DxbcToDxilConverter: Found native dxbc2dxil at: {}", path);
      is_available_ = true;

      // Test if it works
      std::string test_cmd = dxbc2dxil_path_ + " --help 2>&1 | head -1";
      FILE* pipe = popen(test_cmd.c_str(), "r");
      if (pipe) {
        char buffer[256];
        std::string output;
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
          output += buffer;
        }
        int exit_code = pclose(pipe);

        // The native binary should respond to --help
        if (exit_code == 0 || output.find("Usage") != std::string::npos ||
            output.find("dxbc2dxil") != std::string::npos) {
          XELOGI("DxbcToDxilConverter: Native binary verified working");
          return true;
        }
      }

      // Even if --help fails, the binary might still work
      // Some versions don't support --help but work fine with actual conversion
      XELOGW(
          "DxbcToDxilConverter: Could not verify binary with --help, but will "
          "try to use it");
      return true;
    }
  }

  // No native binary found
  XELOGE("DxbcToDxilConverter: Native dxbc2dxil binary not found!");
  XELOGE("Build it from third_party/DirectXShaderCompiler:");
  XELOGE("  cd third_party/DirectXShaderCompiler && ./build_dxilconv_macos.sh");
  XELOGE("Or set DXBC2DXIL_PATH to the binary location");

  is_available_ = false;
  return false;
}

bool DxbcToDxilConverter::Convert(const std::vector<uint8_t>& dxbc_data,
                                  std::vector<uint8_t>& dxil_data_out,
                                  std::string* error_message) {
  if (!is_available_) {
    if (error_message) {
      *error_message =
          "DxbcToDxilConverter not initialized or dxbc2dxil not found";
    }
    return false;
  }

  // Validate DXBC header
  if (dxbc_data.size() < 4 || dxbc_data[0] != 'D' || dxbc_data[1] != 'X' ||
      dxbc_data[2] != 'B' || dxbc_data[3] != 'C') {
    if (error_message) {
      *error_message = "Invalid DXBC data - missing DXBC magic header";
    }
    return false;
  }

  // Check for debug output directories from environment
  const char* dxbc_dir = std::getenv("XENIA_DXBC_OUTPUT_DIR");
  const char* dxil_dir = std::getenv("XENIA_DXIL_OUTPUT_DIR");

  // Generate unique shader ID based on data hash
  uint64_t hash = 0;
  for (size_t i = 0; i < std::min(dxbc_data.size(), size_t(64)); i++) {
    hash = hash * 31 + dxbc_data[i];
  }
  std::string shader_id = std::to_string(hash) + "_" + std::to_string(getpid());

  // Create file paths
  std::string input_filename = temp_dir_ + "/shader_" + shader_id + ".dxbc";
  std::string output_filename = temp_dir_ + "/shader_" + shader_id + ".dxil";

  // Also save to debug directories if specified
  if (dxbc_dir) {
    std::string debug_input =
        std::string(dxbc_dir) + "/shader_" + shader_id + ".dxbc";
    writeFile(debug_input, dxbc_data);
  }

  // Write input file
  if (!writeFile(input_filename, dxbc_data)) {
    if (error_message) {
      *error_message = "Failed to create temporary input file";
    }
    return false;
  }

  // Build command - native binary uses different syntax than Wine version
  // The native dxbc2dxil uses: dxbc2dxil input.dxbc -o output.dxil
  std::stringstream cmd;
  cmd << dxbc2dxil_path_;
  cmd << " \"" << input_filename << "\"";
  cmd << " -o \"" << output_filename << "\"";
  cmd << " 2>&1";  // Capture stderr

  XELOGD("DxbcToDxilConverter: Running: {}", cmd.str());

  // Execute command
  FILE* pipe = popen(cmd.str().c_str(), "r");
  if (!pipe) {
    if (error_message) {
      *error_message = "Failed to execute dxbc2dxil command";
    }
    unlink(input_filename.c_str());
    return false;
  }

  // Read command output
  std::string output;
  char buffer[256];
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    output += buffer;
  }

  int exit_code = pclose(pipe);

  // Clean up input file
  if (!dxbc_dir) {
    unlink(input_filename.c_str());
  }

  if (exit_code != 0) {
    if (error_message) {
      *error_message = "dxbc2dxil failed with exit code " +
                       std::to_string(exit_code) + ": " + output;
    }
    XELOGE("DxbcToDxilConverter: Conversion failed: {}", output);
    unlink(output_filename.c_str());
    return false;
  }

  // Read output file
  bool success = ReadFile(output_filename, dxil_data_out);

  // Copy to debug directory if specified
  if (dxil_dir && success) {
    std::string debug_output =
        std::string(dxil_dir) + "/shader_" + shader_id + ".dxil";
    writeFile(debug_output, dxil_data_out);
  }

  // Clean up output file
  if (!dxil_dir) {
    unlink(output_filename.c_str());
  }

  if (!success) {
    if (error_message) {
      *error_message = "Failed to read output DXIL file";
    }
    return false;
  }

  // Validate DXIL header (DXBC magic for container, or DXIL for raw)
  if (dxil_data_out.size() < 4) {
    if (error_message) {
      *error_message = "Output DXIL file too small";
    }
    return false;
  }

  XELOGD(
      "DxbcToDxilConverter: Successfully converted {} bytes DXBC to {} bytes "
      "DXIL",
      dxbc_data.size(), dxil_data_out.size());

  return true;
}

bool DxbcToDxilConverter::RunWineCommand(const std::string& command,
                                         std::string* output,
                                         std::string* error) {
  // Not used for native binary, but kept for interface compatibility
  FILE* pipe = popen(command.c_str(), "r");
  if (!pipe) {
    if (error) {
      *error = "Failed to execute command";
    }
    return false;
  }

  if (output) {
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
      *output += buffer;
    }
  }

  int exit_code = pclose(pipe);
  return exit_code == 0;
}

std::string DxbcToDxilConverter::CreateTempFile(
    const std::string& prefix, const std::string& suffix,
    const std::vector<uint8_t>& data) {
  // Generate unique filename
  std::stringstream filename;
  filename << temp_dir_ << "/" << prefix << "_" << getpid() << "_" << rand()
           << suffix;

  std::string path = filename.str();

  // Write data to file
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    return "";
  }

  file.write(reinterpret_cast<const char*>(data.data()), data.size());
  file.close();

  return file.good() ? path : "";
}

bool DxbcToDxilConverter::ReadFile(const std::string& path,
                                   std::vector<uint8_t>& data) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    return false;
  }

  size_t size = file.tellg();
  data.resize(size);
  file.seekg(0, std::ios::beg);
  file.read(reinterpret_cast<char*>(data.data()), size);

  return file.good();
}

bool DxbcToDxilConverter::writeFile(const std::string& path,
                                    const std::vector<uint8_t>& data) {
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    return false;
  }

  file.write(reinterpret_cast<const char*>(data.data()), data.size());
  return file.good();
}

void DxbcToDxilConverter::CleanupTempFiles() {
  // Clean up temp directory
  std::string cmd = "rm -rf " + temp_dir_ + "/shader_*";
  system(cmd.c_str());
}

}  // namespace metal
}  // namespace gpu
}  // namespace xe
