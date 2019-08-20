/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_CONTENT_MANAGER_H_
#define XENIA_KERNEL_XAM_CONTENT_MANAGER_H_

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "xenia/base/memory.h"
#include "xenia/base/mutex.h"
#include "xenia/kernel/xam/content_package.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
class KernelState;
}  // namespace kernel
}  // namespace xe

namespace xe {
namespace kernel {
namespace xam {

struct XCONTENT_DATA {
  static const size_t kSize = 4 + 4 + 128 * 2 + 42 + 2;  // = 306 + 2b padding
  uint32_t device_id;
  uint32_t content_type;
  std::wstring display_name;  // 128 chars
  std::string file_name;

  XCONTENT_DATA() = default;
  explicit XCONTENT_DATA(const uint8_t* ptr) {
    device_id = xe::load_and_swap<uint32_t>(ptr + 0);
    content_type = xe::load_and_swap<uint32_t>(ptr + 4);
    display_name = xe::load_and_swap<std::wstring>(ptr + 8);
    file_name = xe::load_and_swap<std::string>(ptr + 8 + 128 * 2);
  }

  void Write(uint8_t* ptr) {
    xe::store_and_swap<uint32_t>(ptr + 0, device_id);
    xe::store_and_swap<uint32_t>(ptr + 4, content_type);
    xe::store_and_swap<std::wstring>(ptr + 8, display_name);
    xe::store_and_swap<std::string>(ptr + 8 + 128 * 2, file_name);
  }
};

class ContentManager {
 public:
  // Extension to append to folder path when searching for STFS headers
  static constexpr const wchar_t* const kStfsHeadersExtension = L".headers.bin";

  ContentManager(KernelState* kernel_state, std::wstring root_path);
  ~ContentManager();

  std::vector<XCONTENT_DATA> ListContent(uint32_t device_id,
                                         uint32_t content_type);

  ContentPackage* ResolvePackage(const XCONTENT_DATA& data);

  bool ContentExists(const XCONTENT_DATA& data);
  X_RESULT CreateContent(std::string root_name, const XCONTENT_DATA& data);
  X_RESULT OpenContent(std::string root_name, const XCONTENT_DATA& data);
  X_RESULT CloseContent(std::string root_name);
  X_RESULT GetContentThumbnail(const XCONTENT_DATA& data,
                               std::vector<uint8_t>* buffer);
  X_RESULT SetContentThumbnail(const XCONTENT_DATA& data,
                               std::vector<uint8_t> buffer);
  X_RESULT DeleteContent(const XCONTENT_DATA& data);
  std::wstring ResolveGameUserContentPath();

  void SetTitleIdOverride(uint32_t title_id) { title_id_override_ = title_id; }

 private:
  uint32_t title_id();

  std::wstring ResolvePackageRoot(uint32_t content_type);
  std::wstring ResolvePackagePath(const XCONTENT_DATA& data);

  KernelState* kernel_state_;
  std::wstring root_path_;

  // TODO(benvanik): remove use of global lock, it's bad here!
  xe::global_critical_region global_critical_region_;
  std::vector<ContentPackage*> open_packages_;

  uint32_t title_id_override_ =
      0;  // can be used for games/apps that request content for other IDs
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_CONTENT_MANAGER_H_
