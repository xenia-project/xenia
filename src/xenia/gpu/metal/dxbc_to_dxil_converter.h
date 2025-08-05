/**
 * DXBC to DXIL converter wrapper for Metal backend
 * Uses Wine to run dxbc2dxil.exe for shader conversion
 */

#ifndef DXBC_TO_DXIL_CONVERTER_H_
#define DXBC_TO_DXIL_CONVERTER_H_

#include <vector>
#include <string>
#include <cstdint>

namespace xe {
namespace gpu {
namespace metal {

class DxbcToDxilConverter {
 public:
  DxbcToDxilConverter();
  ~DxbcToDxilConverter();

  // Initialize the converter (check for Wine and dxbc2dxil.exe)
  bool Initialize();

  // Convert DXBC bytecode to DXIL bytecode
  // Returns true on success, false on failure
  bool Convert(const std::vector<uint8_t>& dxbc_data,
               std::vector<uint8_t>& dxil_data_out,
               std::string* error_message = nullptr);

  // Check if the converter is available
  bool IsAvailable() const { return is_available_; }

  // Get the path to dxbc2dxil.exe
  const std::string& GetDxbc2DxilPath() const { return dxbc2dxil_path_; }

 private:
  bool is_available_ = false;
  std::string wine_path_;
  std::string dxbc2dxil_path_;
  std::string temp_dir_;

  // Helper to run Wine command
  bool RunWineCommand(const std::string& command,
                      std::string* output = nullptr,
                      std::string* error = nullptr);

  // Create a temporary file with the given data
  std::string CreateTempFile(const std::string& prefix,
                             const std::string& suffix,
                             const std::vector<uint8_t>& data);

  // Read file into vector
  bool ReadFile(const std::string& path, std::vector<uint8_t>& data);
  
  // Write vector to file
  bool writeFile(const std::string& path, const std::vector<uint8_t>& data);

  // Clean up temporary files
  void CleanupTempFiles();
};

}  // namespace metal
}  // namespace gpu
}  // namespace xe

#endif  // DXBC_TO_DXIL_CONVERTER_H_