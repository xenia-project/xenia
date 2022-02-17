/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
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
  static WildcardFlags FIRST_AND_LAST;
};

class WildcardRule {
 public:
  WildcardRule(const std::string_view match, const WildcardFlags& flags);
  bool Check(const std::string_view lower,
             std::string_view::size_type* offset) const;

 private:
  std::string match_;
  WildcardFlags rules_;
};

class WildcardEngine {
 public:
  void SetRule(const std::string_view pattern);

  // Always ignoring case
  bool Match(const std::string_view str) const;

 private:
  std::vector<WildcardRule> rules_;
  void PreparePattern(const std::string_view pattern);
};

}  // namespace filesystem
}  // namespace xe

#endif  // XENIA_BASE_FILESYSTEM_WILDCARD_H_
