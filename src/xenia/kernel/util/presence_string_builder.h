/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_UTIL_PRESENCE_STRING_BUILDER_H_
#define XENIA_KERNEL_UTIL_PRESENCE_STRING_BUILDER_H_

#include <cstdint>
#include <map>
#include <optional>
#include <queue>
#include <regex>
#include <string>
#include <string_view>

#include "xenia/kernel/util/xlast.h"

namespace xe {
namespace kernel {
namespace util {

class AttributeStringFormatter {
 public:
  ~AttributeStringFormatter();

  AttributeStringFormatter(std::string_view attribute_string,
                           XLast* title_xlast,
                           std::map<uint32_t, uint32_t> contexts);

  bool IsValid() const { return true; }
  std::string GetPresenceString() const { return presence_string_; }

 private:
  enum class AttributeType { Context = 0, Property = 1, Unknown = 255 };

  const std::regex presence_id_extract_from_specifier =
      std::regex("\\{c(\\d+)\\}");
  const std::regex format_specifier_replace_fragment_regex_ =
      std::regex(R"(\{c\d+\}|\{p0x\d+\})");

  bool ParseAttributeString();
  void BuildPresenceString();

  std::string GetStringFromSpecifier(std::string_view specifier) const;
  std::queue<std::string> GetPresenceFormatSpecifiers() const;

  AttributeType GetAttributeTypeFromSpecifier(std::string_view specifier) const;
  std::optional<uint32_t> GetAttributeIdFromSpecifier(
      const std::string& specifier,
      const AttributeStringFormatter::AttributeType specifier_type) const;

  const std::string attribute_string_;
  std::map<std::string, std::string> attribute_to_string_mapping_;

  std::string presence_string_;

  std::map<uint32_t, uint32_t> contexts_;

  XLast* title_xlast_;

  // Tests
  //
  // std::map<uint32_t, std::string> contexts_ = {
  //    {0, "Context 0"}, {1, "Context 1"}, {2, "Context 2"}};

  // std::map<uint32_t, std::string> properties_ = {
  //     {0x10000001, "Prop 0"}, {0x20000002, "Prop 2"}, {0x30000001, "Prop
  //     3"}};
};

}  // namespace util
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_UTIL_PRESENCE_STRING_BUILDER_H_