/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
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
#include "xenia/base/string_key.h"
#include "xenia/base/string_util.h"
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
  be<uint32_t> device_id;
  be<uint32_t> content_type;
  union {
    be<uint16_t> display_name[128];
    char16_t display_name_chars[128];
  };
  char file_name[42];
  uint8_t padding[2];
};
static_assert_size(XCONTENT_DATA, 308);

struct XCONTENT_AGGREGATE_DATA {
  be<uint32_t> device_id;
  be<uint32_t> content_type;
  union {
    be<uint16_t> display_name[128];
    char16_t display_name_chars[128];
  };
  char file_name[42];
  uint8_t padding[2];
  be<uint32_t> title_id;
};
static_assert_size(XCONTENT_AGGREGATE_DATA, 312);

struct ContentData {
  uint32_t device_id;
  uint32_t content_type;
  std::u16string display_name;
  std::string file_name;

  ContentData() = default;

  bool operator==(const ContentData& rhs) const {
    return device_id == rhs.device_id && content_type == rhs.content_type &&
           file_name == rhs.file_name;
  }

  explicit ContentData(const XCONTENT_DATA& data) {
    device_id = data.device_id;
    content_type = data.content_type;
    display_name = xe::load_and_swap<std::u16string>(data.display_name);
    file_name = xe::load_and_swap<std::string>(data.file_name);
  }

  void Write(XCONTENT_DATA* data) const {
    data->device_id = device_id;
    data->content_type = content_type;
    xe::string_util::copy_and_swap_truncating(
        data->display_name_chars, display_name,
        xe::countof(data->display_name_chars));
    xe::string_util::copy_maybe_truncating<
        string_util::Safety::IKnowWhatIAmDoing>(data->file_name, file_name,
                                                xe::countof(data->file_name));
  }
};

struct ContentAggregateData {
  uint32_t device_id;
  uint32_t content_type;
  std::u16string display_name;
  std::string file_name;
  uint32_t title_id;

  ContentAggregateData() = default;

  explicit ContentAggregateData(const XCONTENT_AGGREGATE_DATA& data) {
    device_id = data.device_id;
    content_type = data.content_type;
    display_name = xe::load_and_swap<std::u16string>(data.display_name);
    file_name = xe::load_and_swap<std::string>(data.file_name);
    title_id = data.title_id;
  }

  void Write(XCONTENT_AGGREGATE_DATA* data) const {
    data->device_id = device_id;
    data->content_type = content_type;
    xe::string_util::copy_and_swap_truncating(
        data->display_name_chars, display_name,
        xe::countof(data->display_name_chars));
    xe::string_util::copy_maybe_truncating<
        string_util::Safety::IKnowWhatIAmDoing>(data->file_name, file_name,
                                                xe::countof(data->file_name));
    data->title_id = title_id;
  }
};

class ContentPackage {
 public:
  ContentPackage(KernelState* kernel_state, const std::string_view root_name,
                 const ContentData& data,
                 const std::filesystem::path& package_path);
  ~ContentPackage();

  const ContentData& GetPackageContentData() const { return content_data_; }

 private:
  KernelState* kernel_state_;
  std::string root_name_;
  std::string device_path_;
  ContentData content_data_;
};

class ContentManager {
 public:
  ContentManager(KernelState* kernel_state,
                 const std::filesystem::path& root_path);
  ~ContentManager();

  std::vector<ContentData> ListContent(uint32_t device_id,
                                       uint32_t content_type);

  std::unique_ptr<ContentPackage> ResolvePackage(
      const std::string_view root_name, const ContentData& data);

  bool ContentExists(const ContentData& data);
  X_RESULT CreateContent(const std::string_view root_name,
                         const ContentData& data);
  X_RESULT OpenContent(const std::string_view root_name,
                       const ContentData& data);
  X_RESULT CloseContent(const std::string_view root_name);
  X_RESULT GetContentThumbnail(const ContentData& data,
                               std::vector<uint8_t>* buffer);
  X_RESULT SetContentThumbnail(const ContentData& data,
                               std::vector<uint8_t> buffer);
  X_RESULT DeleteContent(const ContentData& data);
  std::filesystem::path ResolveGameUserContentPath();
  bool IsContentOpen(const ContentData& data) const;
  void CloseOpenedFilesFromContent(const std::string_view root_name);

 private:
  std::filesystem::path ResolvePackageRoot(uint32_t content_type);
  std::filesystem::path ResolvePackagePath(const ContentData& data);

  KernelState* kernel_state_;
  std::filesystem::path root_path_;

  // TODO(benvanik): remove use of global lock, it's bad here!
  xe::global_critical_region global_critical_region_;
  std::unordered_map<string_key, ContentPackage*> open_packages_;
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_CONTENT_MANAGER_H_
