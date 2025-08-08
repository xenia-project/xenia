/**
 * DXBC to DXIL converter implementation
 */

#include "dxbc_to_dxil_converter.h"

#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <unistd.h>
#include <sys/stat.h>

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

DxbcToDxilConverter::~DxbcToDxilConverter() {
  CleanupTempFiles();
}

bool DxbcToDxilConverter::Initialize() {
  // Check if Wine is available
  wine_path_ = "wine";
  if (system("which wine > /dev/null 2>&1") != 0) {
    printf("[DxbcToDxilConverter] Wine not found in PATH, will hardcode path: /opt/homebrew/bin/wine\n");
      wine_path_ = "/opt/homebrew/bin/wine";
  }

  // Look for dxbc2dxil.exe in various locations
  std::vector<std::string> search_paths = {
    // third_party/dxbc2dxil directory (preferred)
    "../../../../third_party/dxbc2dxil/bin/dxbc2dxil.exe",
    "../../../third_party/dxbc2dxil/bin/dxbc2dxil.exe",
    "../../third_party/dxbc2dxil/bin/dxbc2dxil.exe",
    "../third_party/dxbc2dxil/bin/dxbc2dxil.exe",
    "./third_party/dxbc2dxil/bin/dxbc2dxil.exe",
    // Windows binaries directory
    "./windows_binaries/dxbc2dxil.exe",
    "./test_dxbc2dxil_build/windows_binaries/dxbc2dxil.exe",
    "../test_dxbc2dxil_build/windows_binaries/dxbc2dxil.exe",
    // Local build directory
    "./dxbc2dxil.exe",
    "./test_dxbc2dxil_build/dxbc2dxil.exe",
    "../test_dxbc2dxil_build/dxbc2dxil.exe",
    "../../test_dxbc2dxil_build/dxbc2dxil.exe",
    // Stub for testing
    "./dxbc2dxil_stub.sh",
    "./test_dxbc2dxil_build/dxbc2dxil_stub.sh",
    // DirectXShaderCompiler build outputs
    "./third_party/DirectXShaderCompiler/build/bin/dxbc2dxil.exe",
    "../third_party/DirectXShaderCompiler/build/bin/dxbc2dxil.exe",
    // User-provided location
    std::string(std::getenv("DXBC2DXIL_PATH") ? std::getenv("DXBC2DXIL_PATH") : "")
  };

  for (const auto& path : search_paths) {
    if (!path.empty() && access(path.c_str(), F_OK) == 0) {
      dxbc2dxil_path_ = path;
      printf("[DxbcToDxilConverter] Found dxbc2dxil.exe at: %s\n", path.c_str());
      is_available_ = true;
      
      // Test if it's executable
      if (path.find(".sh") != std::string::npos) {
        // It's a shell script (stub), test directly
        std::string test_cmd = "bash " + dxbc2dxil_path_ + " 2>&1 | head -1";
        if (system(test_cmd.c_str()) == 0) {
          printf("[DxbcToDxilConverter] Using stub script for testing\n");
          return true;
        }
      } else {
        // It's an exe, test with Wine
        // dxbc2dxil.exe uses /? for help, not -help
        std::string test_cmd = wine_path_ + " " + dxbc2dxil_path_ + " /? 2>&1 | grep -q \"Usage:\"";
        if (system(test_cmd.c_str()) == 0) {
          printf("[DxbcToDxilConverter] Wine can run dxbc2dxil.exe successfully\n");
          return true;
        } else {
          printf("[DxbcToDxilConverter] Wine failed to run dxbc2dxil.exe\n");
          is_available_ = false;
        }
      }
    }
  }

  if (!is_available_) {
    printf("[DxbcToDxilConverter] dxbc2dxil.exe not found!\n");
    printf("Please build dxbc2dxil.exe and place it in one of these locations:\n");
    for (const auto& path : search_paths) {
      if (!path.empty()) {
        printf("  - %s\n", path.c_str());
      }
    }
    printf("Or set DXBC2DXIL_PATH environment variable\n");
  }

  return is_available_;
}

bool DxbcToDxilConverter::Convert(const std::vector<uint8_t>& dxbc_data,
                                  std::vector<uint8_t>& dxil_data_out,
                                  std::string* error_message) {
  if (!is_available_) {
    if (error_message) {
      *error_message = "DxbcToDxilConverter not initialized or dxbc2dxil.exe not found";
    }
    return false;
  }

  // Check for debug output directories from environment
  const char* dxbc_dir = std::getenv("XENIA_DXBC_OUTPUT_DIR");
  const char* dxil_dir = std::getenv("XENIA_DXIL_OUTPUT_DIR");
  
  // Generate unique shader ID
  std::string shader_id = std::to_string(getpid()) + "_" + std::to_string(rand());
  
  // Create file paths (use debug directories if available, otherwise current dir)
  std::string input_filename = dxbc_dir 
    ? std::string(dxbc_dir) + "/shader_" + shader_id + ".dxbc"
    : "temp_shader_" + shader_id + ".dxbc";
  std::string output_filename = dxil_dir
    ? std::string(dxil_dir) + "/shader_" + shader_id + ".dxil" 
    : "temp_shader_" + shader_id + ".dxil";
  
  // Write input file
  if (!writeFile(input_filename, dxbc_data)) {
    if (error_message) {
      *error_message = "Failed to create temporary input file";
    }
    return false;
  }
  
  std::string input_path = input_filename;
  std::string output_path = output_filename;

  // Build command based on file type
  std::stringstream cmd;
  if (dxbc2dxil_path_.find(".sh") != std::string::npos) {
    // Shell script
    cmd << "bash " << dxbc2dxil_path_;
  } else {
    // Windows exe with Wine
    // Set DLL path for Wine if using third_party location
    if (dxbc2dxil_path_.find("third_party/dxbc2dxil") != std::string::npos) {
      // Extract directory path
      size_t pos = dxbc2dxil_path_.find_last_of("/\\");
      std::string bin_dir = dxbc2dxil_path_.substr(0, pos);
      cmd << "WINEPATH=\"" << bin_dir << "\" ";
    }
    cmd << wine_path_ << " " << dxbc2dxil_path_;
  }
  cmd << " \"" << input_path << "\"";
  cmd << " /o \"" << output_path << "\"";
  cmd << " 2>&1";  // Capture stderr

  printf("[DxbcToDxilConverter] Running: %s\n", cmd.str().c_str());

  // Execute Wine command
  FILE* pipe = popen(cmd.str().c_str(), "r");
  if (!pipe) {
    if (error_message) {
      *error_message = "Failed to execute Wine command";
    }
    unlink(input_path.c_str());
    return false;
  }

  // Read command output
  std::string output;
  char buffer[256];
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    output += buffer;
  }

  int exit_code = pclose(pipe);
  
  // Clean up input file (unless in debug mode)
  if (!dxbc_dir) {
    unlink(input_path.c_str());
  }

  if (exit_code != 0) {
    if (error_message) {
      *error_message = "dxbc2dxil.exe failed: " + output;
    }
    printf("[DxbcToDxilConverter] Conversion failed: %s\n", output.c_str());
    if (!dxil_dir) {
      unlink(output_path.c_str());  // Clean up in case of partial output
    }
    return false;
  }

  // Read output file
  bool success = ReadFile(output_path, dxil_data_out);
  
  // Clean up output file (unless in debug mode)
  if (!dxil_dir) {
    unlink(output_path.c_str());
  }

  if (!success) {
    if (error_message) {
      *error_message = "Failed to read output DXIL file";
    }
    return false;
  }

  printf("[DxbcToDxilConverter] Successfully converted %zu bytes DXBC to %zu bytes DXIL\n",
         dxbc_data.size(), dxil_data_out.size());


  return true;
}

bool DxbcToDxilConverter::RunWineCommand(const std::string& command,
                                         std::string* output,
                                         std::string* error) {
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

std::string DxbcToDxilConverter::CreateTempFile(const std::string& prefix,
                                                const std::string& suffix,
                                                const std::vector<uint8_t>& data) {
  // Generate unique filename
  std::stringstream filename;
  filename << temp_dir_ << "/" << prefix << "_" << getpid() << "_" 
           << rand() << suffix;
  
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
