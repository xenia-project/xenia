/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/fuzzy.h"

#include <cctype>
#include <cstring>
#include <iostream>

// TODO(gibbed): UTF8 support.

namespace xe {

int fuzzy_match(const std::string_view pattern, const char* value) {
  // https://github.com/mattyork/fuzzy/blob/master/lib/fuzzy.js
  // TODO(benvanik): look at
  // https://github.com/atom/fuzzaldrin/tree/master/src This does not weight
  // complete substrings or prefixes right, which kind of sucks.
  size_t pattern_index = 0;
  size_t value_length = std::strlen(value);
  int total_score = 0;
  int local_score = 0;
  for (size_t i = 0; i < value_length; ++i) {
    if (std::tolower(value[i]) == std::tolower(pattern[pattern_index])) {
      ++pattern_index;
      local_score += 1 + local_score;
    } else {
      local_score = 0;
    }
    total_score += local_score;
  }
  return total_score;
}

std::vector<std::pair<size_t, int>> fuzzy_filter(const std::string_view pattern,
                                                 const void* const* entries,
                                                 size_t entry_count,
                                                 size_t string_offset) {
  std::vector<std::pair<size_t, int>> results;
  results.reserve(entry_count);
  for (size_t i = 0; i < entry_count; ++i) {
    auto entry_value =
        reinterpret_cast<const char*>(entries[i]) + string_offset;
    int score = fuzzy_match(pattern, entry_value);
    results.emplace_back(i, score);
  }
  return results;
}

}  // namespace xe
