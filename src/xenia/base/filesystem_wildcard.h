/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_FILESYSTEM_WILDCARD_H_
#define XENIA_BASE_FILESYSTEM_WILDCARD_H_

#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "xenia/base/string.h"

namespace xe {
namespace filesystem {

class WildcardFlags {
 public:
  bool FromStart : 1, ToEnd : 1, ExactLength : 1;

  WildcardFlags();
  WildcardFlags(bool start, bool end, bool exact_length);

  static WildcardFlags FIRST;
  static WildcardFlags LAST;
  static WildcardFlags ANY;
};

class WildcardRule {
 public:
  WildcardRule(const std::string& str_match, const WildcardFlags& flags);
  bool Check(const std::string& str_lower,
             std::string::size_type* offset) const;

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

#endif  // XENIA_BASE_FILESYSTEM_WILDCARD_H_
