/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/util/gameinfo_utils.h"

namespace xe {
namespace kernel {
namespace util {

constexpr fourcc_t kGameInfoExecSignature = make_fourcc("EXEC");
constexpr fourcc_t kGameInfoCommSignature = make_fourcc("COMM");
constexpr fourcc_t kGameInfoTitlSignature = make_fourcc("TITL");

GameInfoWrapper::GameInfoWrapper(const uint8_t* data, size_t data_size)
    : data_(data), data_size_(data_size) {
  if (!data) {
    return;
  }

  const GameInfoBlockHeader* block_header(nullptr);
  size_t data_offset(0);
  while (data_offset < data_size_) {
    block_header =
        reinterpret_cast<const GameInfoBlockHeader*>(data_ + data_offset);
    data_offset += sizeof(GameInfoBlockHeader);

    switch (block_header->magic) {
      case kGameInfoExecSignature:
        exec_.virtual_titleid =
            reinterpret_cast<const char*>(data_ + data_offset);
        data_offset += exec_.VirtualTitleIdLength + 1;
        exec_.module_name = reinterpret_cast<const char*>(data_ + data_offset);
        data_offset += exec_.ModuleNameLength + 1;
        exec_.build_description =
            reinterpret_cast<const char*>(data_ + data_offset);
        data_offset += exec_.BuildDescriptionLength + 1;
        break;
      case kGameInfoCommSignature:
        assert_true(block_header->block_size == sizeof(GameInfoBlockComm));
        comm_ = reinterpret_cast<const GameInfoBlockComm*>(data_ + data_offset);
        data_offset += block_header->block_size;
        break;
      case kGameInfoTitlSignature:
        assert_true(block_header->block_size == sizeof(GameInfoBlockTitl));
        titl_ = reinterpret_cast<const GameInfoBlockTitl*>(data_ + data_offset);
        data_offset += block_header->block_size;
        break;
      default:
        // Unsupported headers
        data_ = nullptr;
        return;
    }
  }

  if ((comm_ == nullptr) || (titl_ == nullptr) ||
      (exec_.virtual_titleid == nullptr)) {
    data_ = nullptr;
  }
}

uint32_t GameInfo::title_id() const { return comm_->title_id; }

std::string GameInfo::virtual_title_id() const {
  size_t virtual_titleid_length(std::strlen(exec_.virtual_titleid));
  return std::string(exec_.virtual_titleid,
                     exec_.virtual_titleid + virtual_titleid_length);
}

std::string GameInfo::module_name() const {
  size_t module_name_length(std::strlen(exec_.module_name));
  return std::string(exec_.module_name, exec_.module_name + module_name_length);
}

}  // namespace util
}  // namespace kernel
}  // namespace xe
