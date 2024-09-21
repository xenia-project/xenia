/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/util/presence_string_builder.h"
#include "xenia/kernel/util/shim_utils.h"

namespace xe {
namespace kernel {
namespace util {

AttributeStringFormatter::~AttributeStringFormatter() {}

AttributeStringFormatter::AttributeStringFormatter(
    std::string_view attribute_string, XLast* title_xlast,
    std::map<uint32_t, uint32_t> contexts)
    : attribute_string_(attribute_string), attribute_to_string_mapping_() {
  contexts_ = contexts;
  title_xlast_ = title_xlast;

  presence_string_ = "";

  if (!ParseAttributeString()) {
    return;
  }

  BuildPresenceString();
}

bool AttributeStringFormatter::ParseAttributeString() {
  auto specifiers = GetPresenceFormatSpecifiers();

  if (specifiers.empty()) {
    return true;
  }

  std::string specifier;
  while (!specifiers.empty()) {
    std::string specifier = specifiers.front();
    attribute_to_string_mapping_[specifier] = GetStringFromSpecifier(specifier);
    specifiers.pop();
  }
  return true;
}

void AttributeStringFormatter::BuildPresenceString() {
  presence_string_ = attribute_string_;

  for (const auto& entry : attribute_to_string_mapping_) {
    presence_string_.replace(presence_string_.find(entry.first),
                             entry.first.length(), entry.second);
  }
}

AttributeStringFormatter::AttributeType
AttributeStringFormatter::GetAttributeTypeFromSpecifier(
    std::string_view specifier) const {
  if (specifier.length() < 3) {
    return AttributeStringFormatter::AttributeType::Unknown;
  }

  const char presence_type = specifier.at(1);
  if (presence_type == 'c') {
    return AttributeStringFormatter::AttributeType::Context;
  }
  if (presence_type == 'p') {
    return AttributeStringFormatter::AttributeType::Property;
  }
  return AttributeStringFormatter::AttributeType::Unknown;
}

std::optional<uint32_t> AttributeStringFormatter::GetAttributeIdFromSpecifier(
    const std::string& specifier,
    const AttributeStringFormatter::AttributeType specifier_type) const {
  std::smatch string_match;
  if (std::regex_search(specifier, string_match,
                        presence_id_extract_from_specifier)) {
    return std::make_optional<uint32_t>(stoi(string_match[1].str()));
  }

  return std::nullopt;
}

std::string AttributeStringFormatter::GetStringFromSpecifier(
    std::string_view specifier) const {
  const AttributeStringFormatter::AttributeType attribute_type =
      GetAttributeTypeFromSpecifier(specifier);

  if (attribute_type == AttributeStringFormatter::AttributeType::Unknown) {
    return "";
  }

  const auto attribute_id =
      GetAttributeIdFromSpecifier(std::string(specifier), attribute_type);
  if (!attribute_id) {
    return "";
  }

  if (attribute_type == AttributeStringFormatter::AttributeType::Context) {
    // TODO: Different handling for contexts and properties
    const auto itr = contexts_.find(attribute_id.value());

    if (itr == contexts_.cend()) {
      auto attribute_placeholder = fmt::format("{{c{}}}", attribute_id.value());

      return attribute_placeholder;
    }

    const auto attribute_string_id =
        title_xlast_->GetContextStringId(attribute_id.value(), itr->second);

    if (!attribute_string_id.has_value()) {
      return "";
    }

    const auto attribute_string = title_xlast_->GetLocalizedString(
        attribute_string_id.value(), XLanguage::kEnglish);

    return xe::to_utf8(attribute_string);
  }

  if (attribute_type == AttributeStringFormatter::AttributeType::Property) {
    return "";
  }

  return "";
}

std::queue<std::string> AttributeStringFormatter::GetPresenceFormatSpecifiers()
    const {
  std::queue<std::string> format_specifiers;

  std::smatch match;

  std::string attribute_string = attribute_string_;

  while (std::regex_search(attribute_string, match,
                           format_specifier_replace_fragment_regex_)) {
    for (const auto& presence : match) {
      format_specifiers.emplace(presence);
    }
    attribute_string = match.suffix().str();
  }
  return format_specifiers;
}
}  // namespace util
}  // namespace kernel
}  // namespace xe