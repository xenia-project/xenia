/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_STRING_KEY_H_
#define XENIA_BASE_STRING_KEY_H_

#include <algorithm>
#include <string>
#include <variant>

#include "utf8.h"

namespace xe {

namespace internal {

struct string_key_base {
 private:
  std::variant<std::string, std::string_view> value_;

 public:
  explicit string_key_base(const std::string_view value) : value_(value) {}
  explicit string_key_base(std::string value) : value_(std::move(value)) {}

  std::string_view view() const {
    return std::holds_alternative<std::string>(value_)
               ? std::get<std::string>(value_)
               : std::get<std::string_view>(value_);
  }
};

}  // namespace internal

struct string_key : internal::string_key_base {
 public:
  explicit string_key(const std::string_view value) : string_key_base(value) {}
  explicit string_key(std::string value) : string_key_base(value) {}

  static string_key create(const std::string_view value) {
    return string_key(std::string(value));
  }

  static string_key create(std::string value) { return string_key(value); }

  bool operator==(const string_key& other) const {
    return other.view() == view();
  }

  size_t hash() const { return utf8::hash_fnv1a(view()); }

  struct Hash {
    size_t operator()(const string_key& t) const { return t.hash(); }
  };
};

struct string_key_insensitive : internal::string_key_base {
 public:
  explicit string_key_insensitive(const std::string_view value)
      : string_key_base(value) {}
  explicit string_key_insensitive(std::string value) : string_key_base(value) {}

  static string_key_insensitive create(const std::string_view value) {
    return string_key_insensitive(std::string(value));
  }

  static string_key_insensitive create(std::string value) {
    return string_key_insensitive(value);
  }

  bool operator==(const string_key_insensitive& other) const {
    return other.view().size() == view().size() &&
           std::ranges::equal(
               other.view(), view(), {},
               [](char ch) {
                 return std::tolower(static_cast<unsigned char>(ch));
               },
               [](char ch) {
                 return std::tolower(static_cast<unsigned char>(ch));
               });
  }

  size_t hash() const { return utf8::hash_fnv1a(view()); }

  struct Hash {
    size_t operator()(const string_key_insensitive& t) const {
      return t.hash();
    }
  };
};

struct string_key_case : internal::string_key_base {
 public:
  explicit string_key_case(const std::string_view value)
      : string_key_base(value) {}
  explicit string_key_case(std::string value) : string_key_base(value) {}

  static string_key_case create(const std::string_view value) {
    return string_key_case(std::string(value));
  }

  static string_key_case create(std::string value) {
    return string_key_case(value);
  }

  bool operator==(const string_key_case& other) const {
    return utf8::equal_case(other.view(), view());
  }

  size_t hash() const { return utf8::hash_fnv1a_case(view()); }

  struct Hash {
    size_t operator()(const string_key_case& t) const { return t.hash(); }
  };
};

}  // namespace xe

namespace std {
template <>
struct hash<xe::string_key> {
  std::size_t operator()(const xe::string_key& t) const { return t.hash(); }
};

template <>
struct hash<xe::string_key_insensitive> {
  std::size_t operator()(const xe::string_key_insensitive& t) const {
    return t.hash();
  }
};

template <>
struct hash<xe::string_key_case> {
  std::size_t operator()(const xe::string_key_case& t) const {
    return t.hash();
  }
};

};  // namespace std

#endif  // XENIA_BASE_STRING_KEY_H_
