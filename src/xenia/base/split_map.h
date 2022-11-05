/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2019 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_SPLIT_MAP_H_
#define XENIA_BASE_SPLIT_MAP_H_
#include <algorithm>
#include <vector>
namespace xe {
/*
        a map structure that is optimized for infrequent
   reallocation/resizing/erasure and frequent searches by key implemented as 2
   std::vectors, one of the keys and one of the values
*/
template <typename TKey, typename TValue>
class split_map {
  using key_vector = std::vector<TKey>;
  using value_vector = std::vector<TValue>;

  key_vector keys_;
  value_vector values_;

 public:
  using my_type = split_map<TKey, TValue>;

  uint32_t IndexForKey(const TKey& k) {
    auto lbound = std::lower_bound(keys_.begin(), keys_.end(), k);
    return static_cast<uint32_t>(lbound - keys_.begin());
  }

  uint32_t size() const { return static_cast<uint32_t>(keys_.size()); }
  key_vector& Keys() { return keys_; }
  value_vector& Values() { return values_; }
  void clear() {
    keys_.clear();
    values_.clear();
  }
  void resize(uint32_t new_size) {
    keys_.resize(static_cast<size_t>(new_size));
    values_.resize(static_cast<size_t>(new_size));
  }

  void reserve(uint32_t new_size) {
    keys_.reserve(static_cast<size_t>(new_size));
    values_.reserve(static_cast<size_t>(new_size));
  }
  const TKey* KeyAt(uint32_t index) const {
    if (index == size()) {
      return nullptr;
    } else {
      return &keys_[index];
    }
  }
  const TValue* ValueAt(uint32_t index) const {
    if (index == size()) {
      return nullptr;
    } else {
      return &values_[index];
    }
  }

  void InsertAt(TKey k, TValue v, uint32_t idx) {
    uint32_t old_size = size();

    values_.insert(values_.begin() + idx, v);
    keys_.insert(keys_.begin() + idx, k);
  }
  void EraseAt(uint32_t idx) {
    uint32_t old_size = size();
    if (idx == old_size) {
      return;  // trying to erase nonexistent entry
    } else {
      values_.erase(values_.begin() + idx);
      keys_.erase(keys_.begin() + idx);
    }
  }
};
}  // namespace xe

#endif  // XENIA_BASE_SPLIT_MAP_H_