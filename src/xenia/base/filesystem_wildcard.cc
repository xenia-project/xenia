/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/filesystem_wildcard.h"
#include "xenia/base/assert.h"

#include <algorithm>

namespace xe {
namespace filesystem {

WildcardFlags WildcardFlags::FIRST(true, false, false);
WildcardFlags WildcardFlags::LAST(false, true, false);
WildcardFlags WildcardFlags::ANY(false, false, true);

WildcardFlags::WildcardFlags()
    : FromStart(false), ToEnd(false), ExactLength(false) {}

WildcardFlags::WildcardFlags(bool start, bool end, bool exact_length)
    : FromStart(start), ToEnd(end), ExactLength(exact_length) {}

WildcardRule::WildcardRule(const std::string& str_match,
                           const WildcardFlags& flags)
    : match(str_match), rules(flags) {
  std::transform(match.begin(), match.end(), match.begin(), tolower);
}

bool WildcardRule::Check(const std::string& str_lower,
                         std::string::size_type* offset) const {
  if (match.empty()) {
    return true;
  }

  if ((str_lower.size() - *offset) < match.size()) {
    return false;
  }

  if (rules.ExactLength) {
    *offset += match.size();
    return true;
  }

  std::string::size_type result(str_lower.find(match, *offset));

  if (result != std::string::npos) {
    if (rules.FromStart && result != *offset) {
      return false;
    }

    if (rules.ToEnd && result != (str_lower.size() - match.size())) {
      return false;
    }

    *offset = (result + match.size());
    return true;
  }

  return false;
}

void WildcardEngine::PreparePattern(const std::string& pattern) {
  rules.clear();

  WildcardFlags flags(WildcardFlags::FIRST);
  size_t n = 0;
  size_t last = 0;
  while ((n = pattern.find_first_of("*?", last)) != pattern.npos) {
    if (last != n) {
      std::string str_str(pattern.substr(last, n - last));
      rules.push_back(WildcardRule(str_str, flags));
    }
    if (pattern[n] == '?') {
      auto end = pattern.find_first_not_of('?', n + 1);
      auto count = end == pattern.npos ? (pattern.size() - n) : (end - n);
      rules.push_back(
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
    rules.push_back(WildcardRule(str_str, WildcardFlags::LAST));
  }
}

void WildcardEngine::SetRule(const std::string& pattern) {
  PreparePattern(pattern);
}

bool WildcardEngine::Match(const std::string& str) const {
  std::string str_lc;
  std::transform(str.begin(), str.end(), std::back_inserter(str_lc), tolower);

  std::string::size_type offset(0);
  for (const auto& rule : rules) {
    if (!(rule.Check(str_lc, &offset))) {
      return false;
    }
  }

  return true;
}

}  // namespace filesystem
}  // namespace xe
