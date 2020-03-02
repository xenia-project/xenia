/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_FUZZY_H_
#define XENIA_BASE_FUZZY_H_

#include <string>
#include <vector>

namespace xe {

// Tests a match against a case-insensitive fuzzy filter.
// Returns the score of the match or 0 if none.
int fuzzy_match(const std::string_view pattern, const char* value);

// Applies a case-insensitive fuzzy filter to the given entries and ranks
// results.
// Entries is a list of pointers to opaque structs, each of which contains a
// char* string at the given offset.
// Returns an unsorted list of {original index, score}.
std::vector<std::pair<size_t, int>> fuzzy_filter(const std::string_view pattern,
                                                 const void* const* entries,
                                                 size_t entry_count,
                                                 size_t string_offset);
template <typename T>
std::vector<std::pair<size_t, int>> fuzzy_filter(const std::string_view pattern,
                                                 const std::vector<T>& entries,
                                                 size_t string_offset) {
  return fuzzy_filter(pattern, reinterpret_cast<void* const*>(entries.data()),
                      entries.size(), string_offset);
}

}  // namespace xe

#endif  // XENIA_BASE_FUZZY_H_
