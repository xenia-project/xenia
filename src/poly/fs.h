/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef POLY_FS_H_
#define POLY_FS_H_

#include <string>

#include "poly/string.h"

#include <vector>
#include <iterator>

namespace poly {
namespace fs {

bool PathExists(const std::wstring& path);

bool CreateFolder(const std::wstring& path);

bool DeleteFolder(const std::wstring& path);

struct FileInfo {
  enum class Type {
    kFile,
    kDirectory,
  };
  Type type;
  std::wstring name;
  size_t total_size;
};
std::vector<FileInfo> ListFiles(const std::wstring& path);

std::string CanonicalizePath(const std::string& original_path);

class WildcardFlags
{
public:
    bool
      FromStart: 1,
      ToEnd : 1;

    WildcardFlags();
    WildcardFlags(bool start, bool end);

    static WildcardFlags FIRST;
    static WildcardFlags LAST;
};

class WildcardRule
{
public:
    WildcardRule(const std::string& str_match, const WildcardFlags &flags);
    bool Check(const std::string& str_lower, std::string::size_type& offset) const;
  
private:
  std::string match;
    WildcardFlags rules;
};

class WildcardEngine
{
public:
  void SetRule(const std::string &pattern);

  // Always ignoring case
  bool Match(const std::string &str) const;
private:
  std::vector<WildcardRule> rules;
  void PreparePattern(const std::string &pattern);
};

}  // namespace fs
}  // namespace poly

#endif  // POLY_FS_H_
