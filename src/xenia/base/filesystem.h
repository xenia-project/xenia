/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_FILESYSTEM_H_
#define XENIA_BASE_FILESYSTEM_H_

#include <iterator>
#include <string>
#include <vector>

#include "xenia/base/string.h"

namespace xe {
namespace filesystem {

std::string CanonicalizePath(const std::string& original_path);

bool PathExists(const std::wstring& path);

bool CreateFolder(const std::wstring& path);
bool DeleteFolder(const std::wstring& path);
bool IsFolder(const std::wstring& path);

FILE* OpenFile(const std::wstring& path, const char* mode);

struct FileInfo {
  enum class Type {
    kFile,
    kDirectory,
  };
  Type type;
  std::wstring name;
  size_t total_size;
  uint64_t create_timestamp;
  uint64_t access_timestamp;
  uint64_t write_timestamp;
};
std::vector<FileInfo> ListFiles(const std::wstring& path);

class WildcardFlags {
 public:
  bool FromStart : 1, ToEnd : 1;

  WildcardFlags();
  WildcardFlags(bool start, bool end);

  static WildcardFlags FIRST;
  static WildcardFlags LAST;
};

class WildcardRule {
 public:
  WildcardRule(const std::string& str_match, const WildcardFlags& flags);
  bool Check(const std::string& str_lower,
             std::string::size_type& offset) const;

 private:
  std::string match;
  WildcardFlags rules;
};

class WildcardEngine {
 public:
  void SetRule(const std::string& pattern);

  // Always ignoring case
  bool Match(const std::string& str) const;

 private:
  std::vector<WildcardRule> rules;
  void PreparePattern(const std::string& pattern);
};

}  // namespace filesystem
}  // namespace xe

#endif  // XENIA_BASE_FILESYSTEM_H_
