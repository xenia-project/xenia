/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/filesystem_wildcard.h"

#include "xenia/base/assert.h"
#include "xenia/base/string.h"

namespace xe::filesystem {

WildcardFlags WildcardFlags::FIRST(true, false, false);
WildcardFlags WildcardFlags::LAST(false, true, false);
WildcardFlags WildcardFlags::ANY(false, false, true);
WildcardFlags WildcardFlags::FIRST_AND_LAST(true, true, false);

WildcardFlags::WildcardFlags()
    : FromStart(false), ToEnd(false), ExactLength(false) {}

WildcardFlags::WildcardFlags(bool start, bool end, bool exact_length)
    : FromStart(start), ToEnd(end), ExactLength(exact_length) {}

WildcardRule::WildcardRule(const std::string_view match,
                           const WildcardFlags& flags)
    : match_(utf8::lower_ascii(match)), rules_(flags) {}

bool WildcardRule::Check(const std::string_view lower,
                         std::string::size_type* offset) const {
  if (match_.empty()) {
    return true;
  }

  if ((lower.size() - *offset) < match_.size()) {
    return false;
  }

  if (rules_.ExactLength) {
    *offset += match_.size();
    return true;
  }

  std::string_view::size_type result(lower.find(match_, *offset));

  if (result != std::string_view::npos) {
    if (rules_.FromStart && result != *offset) {
      return false;
    }

    if (rules_.ToEnd && result != (lower.size() - match_.size())) {
      return false;
    }

    *offset = (result + match_.size());
    return true;
  }

  return false;
}

void WildcardEngine::PreparePattern(const std::string_view pattern) {
  rules_.clear();

  WildcardFlags flags(WildcardFlags::FIRST);
  size_t n = 0;
  size_t last = 0;
  while ((n = pattern.find_first_of("*?", last)) != pattern.npos) {
    if (last != n) {
      std::string str_str(pattern.substr(last, n - last));
      rules_.push_back(WildcardRule(str_str, flags));
    }
    if (pattern[n] == '?') {
      auto end = pattern.find_first_not_of('?', n + 1);
      auto count = end == pattern.npos ? (pattern.size() - n) : (end - n);
      rules_.push_back(
          WildcardRule(pattern.substr(n, count), WildcardFlags::ANY));
      last = n + count;
    } else if (pattern[n] == '*') {
      last = n + 1;
    } else {
      assert_always();
    }
    flags = WildcardFlags();
  }
  if (last != pattern.size()) {
    std::string str_str(pattern.substr(last));
    rules_.push_back(WildcardRule(
        str_str, last ? WildcardFlags::LAST : WildcardFlags::FIRST_AND_LAST));
  }
}

void WildcardEngine::SetRule(const std::string_view pattern) {
  PreparePattern(pattern);
}

bool WildcardEngine::Match(const std::string_view str) const {
  std::string str_lc = utf8::lower_ascii(str);
  std::string::size_type offset(0);
  for (const auto& rule : rules_) {
    if (!(rule.Check(str_lc, &offset))) {
      return false;
    }
  }

  return true;
}

}  // namespace xe::filesystem
