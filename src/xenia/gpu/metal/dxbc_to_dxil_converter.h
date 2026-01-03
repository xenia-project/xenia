/**
 * DXBC to DXIL converter wrapper for Metal backend.
 *
 * Converts DXBC to DXIL using the in-process dxilconv library (no CLI spawn).
 */

#ifndef DXBC_TO_DXIL_CONVERTER_H_
#define DXBC_TO_DXIL_CONVERTER_H_

#include <cstdint>
#include <string>
#include <vector>

struct IDxbcConverter;

namespace xe {
namespace gpu {
namespace metal {

class DxbcToDxilConverter {
 public:
  DxbcToDxilConverter();
  ~DxbcToDxilConverter();

  // Initialize the converter (ensure dxilconv is available).
  bool Initialize();

  // Convert DXBC bytecode to DXIL bytecode
  // Returns true on success, false on failure
  bool Convert(const std::vector<uint8_t>& dxbc_data,
               std::vector<uint8_t>& dxil_data_out,
               std::string* error_message = nullptr);

  // Check if the converter is available
  bool IsAvailable() const { return is_available_; }

  // Get the dxilconv library path (if resolved).
  const std::string& GetDxbc2DxilPath() const { return dxilconv_path_; }

 private:
  bool is_available_ = false;
  std::string dxilconv_path_;
  std::wstring extra_options_;

  // Lazily created per-thread converter instance.
  IDxbcConverter* GetThreadConverter(std::string* error_message);

  // Write vector to file (for debug dumps).
  bool WriteFile(const std::string& path, const std::vector<uint8_t>& data);
};

}  // namespace metal
}  // namespace gpu
}  // namespace xe

#endif  // DXBC_TO_DXIL_CONVERTER_H_
